# klbody.py DIR... -> per run: Kung Lao body teal pixels in the wide 3-character shots (frames 3600-5400)
# Close-ups of Raiden (lots of white) are skipped; body present when the score > 20.
import sys,os
from PIL import Image
for d in sys.argv[1:]:
    best=0
    for f in sorted(os.listdir(d)):
        if not f.endswith('.png') or not (3600<=int(f[6:11])<=5400): continue
        im=Image.open(os.path.join(d,f)).convert('RGB'); W,H=im.size
        if sum(1 for r,g,b in im.resize((160,120)).get_flattened_data() if r>190 and g>190 and b>190)>=200: continue
        c=im.crop((int(W*.38),int(H*.45),int(W*.56),int(H*.9))).resize((60,70)).get_flattened_data()
        best=max(best,sum(1 for r,g,b in c if g>70 and b>70 and abs(g-b)<45 and g>r+30))
    print(os.path.basename(d),best,'CORPO' if best>20 else 'sem corpo')
