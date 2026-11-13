# Generator for translation/en/info.json: Principal Oishi's office (disc data tree, hidden-content hints)
import sys, json; sys.path.insert(0, 'tools')
from tl_gen import yesno
S = {"_movie": "info.dxr: Principal Oishi explains the disc contents; folder labels in the tree are already English"}
S.update(yesno('info', 13, 14, 15,
    'Principal Oishi: "Oh, out for a stroll? How nice of you to come all this way. Since you\'re here, shall I tell you about the Digital Collection\'s data?"',
    ja='…:…? …?…'))
y58 = yesno('info', 58, 14, 15,
    'Principal Oishi: "Have you seen everything in the school? If you like, I would be happy to show you around."',
    ja='…:…? …')
S["58"] = y58["58"]                      # the hover sprites 14/15 belong to panel 13
folders = {
 22: ("", "* Contains the Digital Collection startup menu.", "※…"),
 24: ("The Satomi Tadashi Pharmacy Song (techno-pop version)", "* Play it on a CD player and enjoy.", "…(…) ※CD…"),
 26: ("Wallpaper Changer", "* The program launched by Mark's Wallpaper Browser.", "… ※…"),
 28: ("Digital Collection launcher", "* The program that starts the Digital Collection.", "…"),
 30: ("QuickTime (32-bit) installer", "* Install it from the Digital Collection startup menu.", "QuickTime(32bit…)…"),
 32: ("Screen saver installer", "* Install them from the Accessories install menu.", "…"),
 34: ("Persona Daily Calendar", "* Install it from the Accessories install menu.", "…"),
 36: ("QuickTime movie data", "* Watch them in Butler Yamaoka's Secret VTR Collection.", "QuickTime… ※…VTR…"),
 38: ("WAV audio (BGM)", "* Listen to them in Ayase's MD Collection.", "WAV音声データ(BGM)"),
 40: ("WAV audio (special attack sound effects)", "* Listen to them in Ayase's MD Collection.", "WAV…(…SE)"),
 42: ("WAV audio (field demons)", "* Listen to them in Kurouri's Spell Card Collection.", "WAV…(…) ※…"),
 44: ("WAV audio (characters)", "* Install them from the Accessories install menu.", "WAV…(…)"),
 46: ("Wallpapers (1024x768)", "* Browse and install them in Mark's Wallpaper Browser.", "壁紙(1024×768用)"),
 48: ("Wallpapers (640x480)", "* Browse and install them in Mark's Wallpaper Browser.", "壁紙(640×480用)"),
 50: ("WAV audio (boss demons)", "* Install them from the Accessories install menu.", "WAV…(…)"),
 52: ("WAV audio (unreleased)", "* Listen to them in Yukino's Photo Exhibition.", "WAV…(…) ※…"),
}
for n, (title, note, ja) in folders.items():
    S[str(n)] = {"ja": ja, "font": "Arial Bold.ttf", "size": 13, "leading": 1.45, "text_box": [10, 9, 450, 54], "en": title + "\n" + note}
dialog = {
 59: ('Principal Oishi: "Yukino really does take wonderful photos. Have you seen them yet?"', '…:…?…'),
 60: ('Principal Oishi: "Ayase collects her favorite songs on MD, you know. Perhaps you\'ll hear some memorable tunes?"', '…:…MD…?…'),
 61: ('Principal Oishi: "I think Kurouri\'s passion for demon research is simply marvelous!"', '…:…!…'),
 62: ('Principal Oishi: "Mr. Yamaoka says there\'s a VTR he simply must show everyone, so he has come back for it."', '…:…VTR…'),
 63: ('Principal Oishi: "I used to play with dress-up dolls as a child too. Now, with Trish\'s magic, you can dress up your favorite characters!"', '…:…'),
}
for n, (en, ja) in dialog.items():
    S[str(n)] = {"ja": ja, "en": en}
S["55"] = {"ja": "…:…?…", "box": [143, 18, 478, 103], "en": 'Principal Oishi: "Which attraction shall I tell you about?"'}
S["18"] = {"ja": "> …", "box": [97, 365, 545, 413], "en": "> Point at a folder to see its description."}
json.dump(S, open('translation/en/info.json', 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
print(len(S))
