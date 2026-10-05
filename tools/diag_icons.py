import re

text = open(r"C:\Users\bryan\Downloads\proj4\proj3\proj\tools\ReflectionMetadata.xml", encoding="utf-8", errors="ignore").read()
hdr = open(r"C:\Users\bryan\Downloads\proj4\proj3\proj\project\core\framework\gui\widgets\explorer\icon_index.hxx", encoding="utf-8").read()

for name in ["GuiService", "RunService", "TweenService", "ContentProvider", "Stats", "LogService", "HttpRbxApiService", "TimerService", "DataModel", "Instance", "ServiceProvider"]:
    print(name, "in map" if f'"{name}"' in hdr else "MISSING")

idxs = [int(x) for x in re.findall(r'ExplorerImageIndex">(\d+)', text)]
print("max idx", max(idxs), "sheet tiles", 2352 // 16)

# Fix parser: for each ReflectionMetadataClass, find Name and ExplorerImageIndex within that Item
# Current regex might fail for nested Items - try non-greedy with Properties section
classes = re.findall(r'<Item class="ReflectionMetadataClass">(.*?)</Item>', text, re.S)
print("class items", len(classes))
# Many Items nest - outer closes late. Better approach:
pairs = {}
for m in re.finditer(r'<string name="Name">([^<]+)</string>\s*<string name="ExplorerOrder">[^<]*</string>\s*<string name="ExplorerImageIndex">(\d+)</string>', text):
    pairs[m.group(1)] = int(m.group(2))
print("ordered pairs", len(pairs))
# alternate: any Name followed within 500 chars by ExplorerImageIndex
pairs2 = {}
for m in re.finditer(r'<string name="Name">([^<]+)</string>', text):
    window = text[m.end():m.end()+800]
    i = re.search(r'<string name="ExplorerImageIndex">(\d+)</string>', window)
    if i and "ReflectionMetadataMember" not in window[:i.start()]:
        # skip if we hit another Name first
        n2 = re.search(r'<string name="Name">', window)
        if n2 and n2.start() < i.start():
            continue
        pairs2[m.group(1)] = int(i.group(1))
print("window pairs", len(pairs2), "GuiService", pairs2.get("GuiService"), "RunService", pairs2.get("RunService"))
