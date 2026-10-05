#include <core/framework/features/visuals/chams/mesh/adapt.hxx>
#include <core/framework/features/visuals/chams/mesh/parser/mesh_parser.hxx>
#include <core/framework/features/visuals/chams/mesh/cache/mesh_cache.hxx>


#include <atomic>
#include <cctype>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace core::features::mesh_stack {
namespace MeshParser {
namespace {

bool IsBasePartClass(const std::string& cls)
{
	return cls == "Part" || cls == "MeshPart" || cls == "UnionOperation" ||
	       cls == "NegateOperation" || cls == "IntersectOperation" ||
	       cls == "TrussPart" || cls == "WedgePart" || cls == "CornerWedgePart" ||
	       cls == "Seat" || cls == "VehicleSeat" || cls == "SpawnLocation";
}

bool IsSkipClass(const std::string& cls)
{
	return cls == "Humanoid" || cls == "Script" || cls == "LocalScript" ||
	       cls == "ModuleScript" || cls == "Sound" || cls == "Animation" ||
	       cls == "Animator" || cls == "BindableEvent" || cls == "BindableFunction" ||
	       cls == "RemoteEvent" || cls == "RemoteFunction" || cls == "Attachment" ||
	       cls == "Motor6D" || cls == "Weld" || cls == "WeldConstraint" ||
	       cls == "ManualWeld" || cls == "Snap" || cls == "BodyColors" ||
	       cls == "Shirt" || cls == "Pants" || cls == "ShirtGraphic" ||
	       cls == "BodyGyro" || cls == "BodyVelocity" || cls == "BodyForce" ||
	       cls == "Highlight" || cls == "BillboardGui" || cls == "SurfaceGui" ||
	       cls == "ProximityPrompt" || cls == "ClickDetector" ||
	       cls == "WrapTarget" || cls == "WrapLayer" || cls == "SurfaceAppearance" ||
	       cls == "NoCollisionConstraint";
}

bool NameHas(const std::string& s, const char* needle)
{
	if (s.empty() || !needle)
		return false;
	std::string a = s;
	std::string b = needle;
	for (char& c : a) c = (char)std::tolower((unsigned char)c);
	for (char& c : b) c = (char)std::tolower((unsigned char)c);
	return a.find(b) != std::string::npos;
}

// CB: CollisionCapsule / hitbox — не визуал
bool IsSkipPartName(const std::string& name)
{
	if (name.empty())
		return false;
	if (name == "HumanoidRootPart" || name == "CollisionCapsule")
		return true;
	return NameHas(name, "collision") || NameHas(name, "hitbox") ||
	       NameHas(name, "capsule") || NameHas(name, "nocol");
}

// CB: оружие / viewmodel не в чамсы персонажа
// НЕ трогаем "arms" — иначе режет папки рук у персонажа → бокс по торсу
bool IsSkipContainerName(const std::string& name)
{
	return NameHas(name, "weapon") || NameHas(name, "gun") ||
	       NameHas(name, "viewmodel") || NameHas(name, "firstperson") ||
	       NameHas(name, "viewarms") || NameHas(name, "fakearm");
}

bool IsAccessoryClass(const std::string& cls)
{
	return cls == "Accessory" || cls == "Hat" || cls == "Accoutrement";
}

Kind Classify(const std::string& part_name, const std::string& container, bool under_acc)
{
	if (under_acc)
	{
		if (NameHas(part_name, "face") || NameHas(container, "face"))
			return Kind::Face;
		if (NameHas(container, "hair") || NameHas(part_name, "hair") ||
		    NameHas(container, "ponytail") || NameHas(container, "bun") ||
		    NameHas(container, "beetle") || NameHas(container, "ringo"))
			return Kind::Hair;
		return Kind::Accessory;
	}

	static const char* k_body[] = {
		"Head", "Torso", "UpperTorso", "LowerTorso",
		"LeftUpperArm", "LeftLowerArm", "LeftHand", "Left Arm",
		"RightUpperArm", "RightLowerArm", "RightHand", "Right Arm",
		"LeftUpperLeg", "LeftLowerLeg", "LeftFoot", "Left Leg",
		"RightUpperLeg", "RightLowerLeg", "RightFoot", "Right Leg",
		"HumanoidRootPart"
	};
	for (const char* n : k_body)
	{
		if (part_name == n)
			return Kind::Body;
	}
	return Kind::Other;
}

std::string ReadContentString(std::uint64_t addr)
{
	if (!g_Memory.IsValid(addr))
		return {};
	std::string s = g_Memory.ReadString(addr);
	if (!s.empty() && s != "Unknown")
		return s;
	std::uint64_t p = g_Memory.Read<std::uint64_t>(addr);
	if (!g_Memory.IsValid(p))
		return {};
	s = g_Memory.ReadString(p);
	if (s == "Unknown")
		return {};
	return s;
}

std::string ReadSpecialMeshId(std::uint64_t sm)
{
	return ReadContentString(sm + Offsets::SpecialMesh::MeshId);
}

std::string ReadCharacterMeshId(std::uint64_t cm)
{
	return ReadContentString(cm + Offsets::CharacterMesh::MeshId);
}

std::string ReadMeshPartId(std::uint64_t part)
{
	return MeshPartGetMeshId(part);
}

void PushPart(
	std::uint64_t character,
	const Instance& part,
	const std::string& container,
	bool under_acc,
	std::unordered_set<std::uint64_t>& seen,
	std::vector<Entry>& out)
{
	if (!g_Memory.IsValid(part.address) || seen.count(part.address))
		return;
	seen.insert(part.address);

	Entry e{};
	e.part = part.address;
	e.character = character;
	e.name = part.GetName();
	e.class_name = part.GetClassName();
	e.container = container;
	e.kind = Classify(e.name, container, under_acc);

	if (e.class_name == "MeshPart")
		e.mesh_id = ReadMeshPartId(part.address);

	for (const auto& c : part.GetChildren())
	{
		const std::string cc = c.GetClassName();
		if (cc == "SpecialMesh" || cc == "FileMesh" || cc == "CylinderMesh" || cc == "BlockMesh")
		{
			e.special_mesh = c.address;
			if (e.mesh_id.empty())
				e.mesh_id = ReadSpecialMeshId(c.address);
			if (e.kind == Kind::Other)
				e.kind = Kind::Special;
			break;
		}
	}

	out.push_back(std::move(e));
}

void Walk(
	std::uint64_t character,
	const Instance& node,
	int depth,
	const std::string& container,
	bool under_acc,
	std::unordered_set<std::uint64_t>& seen,
	std::vector<Entry>& out)
{
	if (depth > 12 || !g_Memory.IsValid(node.address))
		return;

	const std::string cls = node.GetClassName();
	if (IsSkipClass(cls))
		return;

	if (cls == "Tool")
		return;

	if (IsAccessoryClass(cls))
	{
		const std::string acc = node.GetName();
		// Handle иногда внутри Model/Folder — обходим всё дерево акса
		for (const auto& c : node.GetChildren())
			Walk(character, c, depth + 1, acc, true, seen, out);
		return;
	}

	// аксы без класса Accessory (редко), но Handle под персонажем
	if (!under_acc && (cls == "Model" || cls == "Folder"))
	{
		const std::string nm = node.GetName();
		// Counter Blox: WeaponModel / WeaponAttachments — не тело
		if (IsSkipContainerName(nm))
			return;
		// CharacterArmor / аксы / layered
		bool maybe_acc = NameHas(nm, "accessory") || NameHas(nm, "hat") ||
			NameHas(nm, "hair") || NameHas(nm, "layer") || NameHas(nm, "mesh") ||
			NameHas(nm, "armor") || NameHas(nm, "clothing") || NameHas(nm, "gear");
		for (const auto& c : node.GetChildren())
			Walk(character, c, depth + 1, maybe_acc ? nm : container, under_acc || maybe_acc, seen, out);
		return;
	}

	if (cls == "CharacterMesh")
	{
		Entry e{};
		e.part = node.address;
		e.character = character;
		e.name = node.GetName();
		e.class_name = cls;
		e.container = container;
		e.kind = Kind::CharacterMesh;
		e.mesh_id = ReadCharacterMeshId(node.address);
		out.push_back(std::move(e));
		return;
	}

	if (IsBasePartClass(cls))
	{
		const std::string nm = node.GetName();
		if (IsSkipPartName(nm))
			return;

		// Handle где угодно под персонажем = акс (даже если родитель не Accessory)
		const bool acc = under_acc || (nm == "Handle");
		PushPart(character, node, container, acc, seen, out);
		for (const auto& c : node.GetChildren())
		{
			const std::string cc = c.GetClassName();
			if (IsBasePartClass(cc) || IsAccessoryClass(cc) ||
			    cc == "Model" || cc == "Folder")
				Walk(character, c, depth + 1, container, acc, seen, out);
		}
		return;
	}

	for (const auto& c : node.GetChildren())
		Walk(character, c, depth + 1, container, under_acc, seen, out);
}

} // namespace

const char* KindName(Kind k)
{
	switch (k)
	{
	case Kind::Body: return "body";
	case Kind::Accessory: return "accessory";
	case Kind::Face: return "face";
	case Kind::Hair: return "hair";
	case Kind::CharacterMesh: return "charmesh";
	case Kind::Special: return "special";
	default: return "other";
	}
}

std::vector<Entry> Collect(std::uint64_t character)
{
	std::vector<Entry> out;
	if (!g_Memory.IsValid(character))
		return out;

	out.reserve(48);
	std::unordered_set<std::uint64_t> seen;
	Walk(character, Instance(character), 0, {}, false, seen, out);
	return out;
}

const char* DefaultLimb(const std::string& name)
{
	if (name == "Torso")
		return "rbxasset://avatar/meshes/torso.mesh";
	if (name == "Left Arm")
		return "rbxasset://avatar/meshes/leftarm.mesh";
	if (name == "Right Arm")
		return "rbxasset://avatar/meshes/rightarm.mesh";
	if (name == "Left Leg")
		return "rbxasset://avatar/meshes/leftleg.mesh";
	if (name == "Right Leg")
		return "rbxasset://avatar/meshes/rightleg.mesh";
	if (name == "Head")
		return "rbxasset://avatar/heads/head.mesh";
	return nullptr;
}

int LimbIndex(const std::string& name)
{
	if (name == "Torso" || name == "UpperTorso" || name == "LowerTorso")
		return 1;
	if (name == "Left Arm" || name == "LeftUpperArm" || name == "LeftLowerArm" || name == "LeftHand")
		return 2;
	if (name == "Right Arm" || name == "RightUpperArm" || name == "RightLowerArm" || name == "RightHand")
		return 3;
	if (name == "Left Leg" || name == "LeftUpperLeg" || name == "LeftLowerLeg" || name == "LeftFoot")
		return 4;
	if (name == "Right Leg" || name == "RightUpperLeg" || name == "RightLowerLeg" || name == "RightFoot")
		return 5;
	return -1;
}

std::vector<Entry> CollectDrawable(std::uint64_t character)
{
	std::vector<Entry> all = Collect(character);
	std::string limb[6];
	std::vector<std::string> wants;
	wants.reserve(all.size() + 8);
	for (const auto& e : all)
	{
		if (e.kind != Kind::CharacterMesh || !e.part || e.mesh_id.empty())
			continue;
		const int bp = g_Memory.Read<int>(e.part + Offsets::CharacterMesh::BodyPart);
		if (bp >= 1 && bp <= 5)
			limb[bp] = e.mesh_id;
		wants.push_back(e.mesh_id);
	}
	std::vector<Entry> out;
	out.reserve(all.size());
	for (auto& e : all)
	{
		if (e.kind == Kind::CharacterMesh)
			continue;
		if (e.mesh_id.empty() && e.kind == Kind::Body)
		{
			const int bp = LimbIndex(e.name);
			if (bp >= 1 && bp <= 5 && !limb[bp].empty())
				e.mesh_id = limb[bp];
			else if (const char* def = DefaultLimb(e.name))
				e.mesh_id = def;
		}
		if (!e.mesh_id.empty())
			wants.push_back(e.mesh_id);
		if (!e.part || !g_Memory.IsValid(e.part))
			continue;
		if (IsSkipPartName(e.name))
			continue;
		if (e.kind == Kind::Other)
		{
			if (e.mesh_id.empty() && !e.special_mesh && e.class_name != "MeshPart")
				continue;
			e.kind = Kind::Accessory;
		}
		out.push_back(std::move(e));
	}
	MeshCache::Get().WantMany(wants);
	return out;
}

std::vector<Entry> CollectForBounds(std::uint64_t character)
{
	std::vector<Entry> all = Collect(character);
	std::vector<Entry> out;
	out.reserve(all.size());
	for (auto& e : all)
	{
		if (!e.part || !g_Memory.IsValid(e.part))
			continue;
		if (IsSkipPartName(e.name))
			continue;
		out.push_back(std::move(e));
	}
	return out;
}

namespace {
std::mutex g_snap_mu;
std::unordered_map<std::uint64_t, std::shared_ptr<const std::vector<Entry>>> g_snap;
std::unordered_map<std::uint64_t, ULONGLONG> g_snap_t;
std::size_t g_cursor = 0;
}

void PumpCharacters(const std::uint64_t* chars, int n)
{
	if (!chars || n <= 0)
		return;

	const ULONGLONG now = GetTickCount64();
	static const char* const k_def[] = {
		"rbxasset://avatar/heads/head.mesh",
		"rbxasset://avatar/meshes/torso.mesh",
		"rbxasset://avatar/meshes/leftarm.mesh",
		"rbxasset://avatar/meshes/rightarm.mesh",
		"rbxasset://avatar/meshes/leftleg.mesh",
		"rbxasset://avatar/meshes/rightleg.mesh",
	};
	for (const char* d : k_def)
		MeshCache::Get().Want(d);

	const int batch = 16;
	std::vector<std::uint64_t> work;
	work.reserve((std::size_t)batch);

	for (int k = 0; k < n && (int)work.size() < batch; ++k)
	{
		const std::uint64_t c = chars[(g_cursor + (std::size_t)k) % (std::size_t)n];
		if (!c)
			continue;
		std::lock_guard<std::mutex> lk(g_snap_mu);
		const auto it = g_snap_t.find(c);
		if (it != g_snap_t.end() && now - it->second < 220ull)
			continue;
		g_snap_t[c] = now;
		work.push_back(c);
	}
	g_cursor += (std::size_t)batch;
	if (n > 0)
		g_cursor %= (std::size_t)n;

	std::vector<std::shared_ptr<const std::vector<Entry>>> got(work.size());
	std::atomic<int> left{ (int)work.size() };
	for (std::size_t i = 0; i < work.size(); ++i)
	{
		const std::uint64_t c = work[i];
		if (!Jobs::Enqueue([&, i, c] {
			got[i] = std::make_shared<const std::vector<Entry>>(CollectDrawable(c));
			left.fetch_sub(1, std::memory_order_acq_rel);
		}))
		{
			got[i] = std::make_shared<const std::vector<Entry>>(CollectDrawable(c));
			left.fetch_sub(1, std::memory_order_acq_rel);
		}
	}
	while (left.load(std::memory_order_acquire) > 0)
		std::this_thread::yield();

	{
		std::lock_guard<std::mutex> lk(g_snap_mu);
		for (std::size_t i = 0; i < work.size(); ++i)
		{
			if (got[i])
				g_snap[work[i]] = std::move(got[i]);
			g_snap_t[work[i]] = now;
		}
	}

	std::lock_guard<std::mutex> lk(g_snap_mu);
	for (auto it = g_snap_t.begin(); it != g_snap_t.end(); )
	{
		if (now - it->second > 4000ull)
		{
			g_snap.erase(it->first);
			it = g_snap_t.erase(it);
		}
		else
			++it;
	}
}

std::shared_ptr<const std::vector<Entry>> CachedDrawable(std::uint64_t character)
{
	std::lock_guard<std::mutex> lk(g_snap_mu);
	const auto it = g_snap.find(character);
	if (it == g_snap.end())
		return nullptr;
	return it->second;
}

} // namespace MeshParser
} // namespace core::features::mesh_stack
