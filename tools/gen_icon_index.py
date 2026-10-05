import os
import re
import urllib.request

url = "https://raw.githubusercontent.com/MaximumADHD/Roblox-Client-Tracker/roblox/ReflectionMetadata.xml"
xml_path = r"C:\Users\bryan\Downloads\proj4\proj3\proj\tools\ReflectionMetadata.xml"
hdr = r"C:\Users\bryan\Downloads\proj4\proj3\proj\project\core\framework\gui\widgets\explorer\icon_index.hxx"

urllib.request.urlretrieve(url, xml_path)
text = open(xml_path, encoding="utf-8", errors="ignore").read()
classes = re.findall(r'<Item class="ReflectionMetadataClass">(.*?)</Item>', text, re.S)
pairs = []
for block in classes:
    m = re.search(r'<string name="Name">([^<]+)</string>', block)
    i = re.search(r'<string name="ExplorerImageIndex">(\d+)</string>', block)
    if m and i:
        pairs.append((m.group(1), int(i.group(1))))

os.makedirs(os.path.dirname(hdr), exist_ok=True)
with open(hdr, "w", encoding="utf-8", newline="\n") as f:
    f.write("#pragma once\n")
    f.write("#include <string>\n")
    f.write("#include <unordered_map>\n\n")
    f.write("namespace core::gui::explorer\n{\n")
    f.write("\tinline const std::unordered_map<std::string, int>& icon_index_map()\n")
    f.write("\t{\n")
    f.write("\t\tstatic const std::unordered_map<std::string, int> m{\n")
    for name, idx in sorted(pairs, key=lambda x: x[0]):
        f.write(f'\t\t\t{{ "{name}", {idx} }},\n')
    f.write("\t\t};\n")
    f.write("\t\treturn m;\n")
    f.write("\t}\n")
    f.write("}\n")

print(f"pairs={len(pairs)}")
