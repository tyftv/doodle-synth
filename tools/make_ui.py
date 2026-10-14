"""Wraps the faceplate page (the same file published as the browser version) into ui/index.html for the plugin."""
import sys, pathlib
src = pathlib.Path(sys.argv[1]).read_text(encoding="utf-8")
head = ('<!doctype html><html lang="en"><head><meta charset="utf-8">'
        '<meta name="viewport" content="width=device-width,initial-scale=1">'
        '<style>:root{color-scheme:dark}body{margin:0;overflow:hidden}[hidden]{display:none!important}img{max-width:100%}</style>'
        '</head><body>')
out = pathlib.Path(__file__).resolve().parent.parent / "ui" / "index.html"
out.write_text(head + src + "</body></html>", encoding="utf-8")
print("wrote", out)
