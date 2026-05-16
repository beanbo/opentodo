"""Apply a {source: translation} JSON dict to a Qt Linguist .ts file.

Usage: python apply_translations.py i18n/opentodo_en.ts translations_en.json

Behaviour:
- For each <message> with <translation type="unfinished"> whose <source> text
  appears as a key in the JSON dict, sets the translation text and removes the
  unfinished marker.
- For numerus messages, the dict value must be a list of three strings — one
  per numerusform child.
- Leaves entries not in the dict alone (still unfinished — will fail compile
  if any remain; the script reports the count).
- Writes the file back with the original XML declaration and DOCTYPE.
"""
import json
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

def apply(ts_path, dict_path):
    tree = ET.parse(ts_path)
    root = tree.getroot()
    with open(dict_path, encoding="utf-8") as fh:
        translations = json.load(fh)

    filled = 0
    skipped = 0
    still_unfinished = 0

    for msg in root.iter("message"):
        source_el = msg.find("source")
        trans_el = msg.find("translation")
        if source_el is None or trans_el is None:
            continue
        is_unfinished = trans_el.get("type") == "unfinished"
        if not is_unfinished:
            continue
        source_text = source_el.text or ""
        is_numerus = msg.get("numerus") == "yes"

        if source_text in translations:
            value = translations[source_text]
            if is_numerus:
                if not isinstance(value, list) or len(value) not in (2, 3):
                    print(f"WARN: numerus entry for {source_text!r} must be a 2- or 3-element list, got {value!r}",
                          file=sys.stderr)
                    skipped += 1
                    continue
                forms = trans_el.findall("numerusform")
                if len(forms) not in (2, 3):
                    print(f"WARN: numerusform count not in (2,3) in .ts for {source_text!r} (got {len(forms)})", file=sys.stderr)
                    skipped += 1
                    continue
                # Map value forms to .ts forms. English has 2 plural forms (singular, plural).
                # If we got a 3-element list but the .ts only has 2 slots, drop the duplicated
                # plural in slot 2 (it's a copy of slot 1 anyway). If the .ts has 3 slots and
                # we have 2 elements, duplicate the plural into the third.
                if len(forms) == len(value):
                    use = value
                elif len(forms) == 2 and len(value) == 3:
                    use = value[:2]
                elif len(forms) == 3 and len(value) == 2:
                    use = list(value) + [value[1]]
                else:
                    use = value  # unreachable given the guards above
                for f, v in zip(forms, use):
                    f.text = v
                trans_el.text = None
            else:
                if not isinstance(value, str):
                    print(f"WARN: non-numerus entry for {source_text!r} must be a string, got {value!r}",
                          file=sys.stderr)
                    skipped += 1
                    continue
                trans_el.text = value
            del trans_el.attrib["type"]
            filled += 1
        else:
            still_unfinished += 1

    raw = ET.tostring(root, encoding="utf-8").decode("utf-8")
    out = ('<?xml version="1.0" encoding="utf-8"?>\n'
           '<!DOCTYPE TS>\n'
           + raw + "\n")
    Path(ts_path).write_text(out, encoding="utf-8")
    print(f"{ts_path}: filled={filled} skipped={skipped} still_unfinished={still_unfinished}")

if __name__ == "__main__":
    apply(sys.argv[1], sys.argv[2])
