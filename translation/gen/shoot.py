# Generator for translation/en/shoot.json: the vice principal's shooting gallery (Game Start!/GAME OVER/Congratulations! are English)
import sys, json; sys.path.insert(0, 'tools')
from tl_gen import yesno, load
from textpanel import find_interior
S = {"_movie": "shoot.dxr: Vice Principal Hanya's shooting gallery: hit only the frog figurines, not the decoy characters"}
S.update(yesno('shoot', 11, 12, 13,
    'Vice Principal Hanya: "This whole incident has made me a changed man. I intend to become an educator the students adore, like the principal. Well? Won\'t you have a little fun?"',
    ja='…:…?…', size=12))
S.update(yesno('shoot', 18, 19, 20, 'Vice Principal Hanya: "Hmm, close... but not quite. Well? Want to try again?"',
               ja='…:…?…'))
def panel(n, en, ja):
    im = load('shoot', n); l, t, r, b = find_interior(im)
    S[str(n)] = {"ja": ja, "box": [l, t, r, b], "text_box": [l + 6, t + 3, r - 4, b], "en": en}
panel(17, 'Vice Principal Hanya: "Quite the marksman, aren\'t you! Very well, here\'s a ticket."', '…:…! …')
panel(52, 'Vice Principal Hanya: "Pests will pop up along the way, so look carefully and shoot only the frog figurines. Hit nothing but frogs, perfectly, and I\'ll give you a ticket as a reward."',
      '…:…1…')
json.dump(S, open('translation/en/shoot.json', 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
print(len(S))
