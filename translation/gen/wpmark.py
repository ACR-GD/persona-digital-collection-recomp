# Generator for translation/en/wpmark.json (Mark's wallpaper exhibit)
import sys, json; sys.path.insert(0, 'tools')
from tl_gen import yesno
S = {"_movie": "wpmark.dxr: Mark's wallpaper (graffiti art) gallery; INSTALL / AUTO PLAY / STOP buttons are already English"}
S.update(yesno('wpmark', 281, 282, 283,
    'Mark: "Whaddaya think? It\'s all about the shout of the soul, y\'know. Come check out my graffiti art!"',
    ja='…:…? …!… > … / …'))
json.dump(S, open('translation/en/wpmark.json', 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
