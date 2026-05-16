"""Extract untranslated source strings from a .ts file.

Usage: python extract_untranslated.py i18n/opentodo_en.ts > untranslated_en.json
Output: JSON array of {source, context, location, numerus} entries.
"""
import json
import sys
import xml.etree.ElementTree as ET

def extract(ts_path):
    tree = ET.parse(ts_path)
    root = tree.getroot()
    entries = []
    for ctx in root.iter("context"):
        ctx_name_el = ctx.find("name")
        ctx_name = ctx_name_el.text if ctx_name_el is not None else ""
        for msg in ctx.findall("message"):
            source_el = msg.find("source")
            trans_el = msg.find("translation")
            if source_el is None or trans_el is None:
                continue
            is_numerus = msg.get("numerus") == "yes"
            is_unfinished = trans_el.get("type") == "unfinished"
            if not is_unfinished:
                continue
            entries.append({
                "context": ctx_name,
                "source": source_el.text or "",
                "numerus": is_numerus,
            })
    return entries

if __name__ == "__main__":
    data = extract(sys.argv[1])
    out_path = sys.argv[2] if len(sys.argv) > 2 else None
    js = json.dumps(data, ensure_ascii=False, indent=2)
    if out_path:
        with open(out_path, "w", encoding="utf-8") as fh:
            fh.write(js)
    else:
        sys.stdout.buffer.write(js.encode("utf-8"))
