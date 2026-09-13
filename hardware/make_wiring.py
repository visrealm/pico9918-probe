"""Standalone, printable prototype wiring drawing; Python + Pillow only."""
from pathlib import Path
from html import escape
import csv
from PIL import Image, ImageDraw, ImageFont

HERE = Path(__file__).resolve().parent
W, H = 1600, 1120
INK, BLUE, TEAL, PURPLE, GREY = '#17324d', '#1763a6', '#087c74', '#7547a5', '#667789'
svg = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}">',
       '<title>Pico 2 to PICO9918 prototype wiring</title>',
       '<desc>Direct bus and SWD wiring, shared Pico 2 USB power through a removable jumper, 40-pin ZIF socket.</desc>']
im = Image.new('RGB', (W*2, H*2), 'white')
draw = ImageDraw.Draw(im)
pdf = ['q 0.744094 0 0 0.744094 0 0 cm']

def rgb(c):
    return ' '.join(f'{int(c[i:i+2],16)/255:.4f}' for i in (1,3,5))

def rect(x,y,w,h,fill='#ffffff',stroke=None):
    svg.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" fill="{fill}" stroke="{stroke or fill}"/>')
    draw.rectangle((x*2,y*2,(x+w)*2,(y+h)*2),fill=fill,outline=stroke,width=2)
    pdf.append(f'{rgb(fill)} rg {rgb(stroke or fill)} RG {x} {H-y-h} {w} {h} re B')

def line(x1,y1,x2,y2,c=INK,width=2):
    svg.append(f'<line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" stroke="{c}" stroke-width="{width}"/>')
    draw.line((x1*2,y1*2,x2*2,y2*2),fill=c,width=width*2)
    pdf.append(f'{rgb(c)} RG {width} w {x1} {H-y1} m {x2} {H-y2} l S')

def filled_arrow(x,y,dx,dy,c):
    """BJT emitter arrow: direction vector points toward the arrow tip."""
    length=(dx*dx+dy*dy)**.5
    ux,uy=dx/length,dy/length
    points=[(x,y),(x-15*ux+6*uy,y-15*uy-6*ux),
            (x-15*ux-6*uy,y-15*uy+6*ux)]
    coords=' '.join(f'{a:.3f},{b:.3f}' for a,b in points)
    svg.append(f'<polygon points="{coords}" fill="{c}"/>')
    draw.polygon([(round(a*2),round(b*2)) for a,b in points],fill=c)
    path=' '.join(f'{a:.3f} {H-b:.3f} '+('m' if i==0 else 'l') for i,(a,b) in enumerate(points))
    pdf.append(f'{rgb(c)} rg {path} h f')

def text(x,y,s,size=18,c=INK,bold=False):
    assert s.isascii(), s
    svg.append(f'<text x="{x}" y="{y}" fill="{c}" font-family="Arial, sans-serif" font-size="{size}" font-weight="{700 if bold else 400}">{escape(s)}</text>')
    font = ImageFont.truetype('C:/Windows/Fonts/' + ('arialbd.ttf' if bold else 'arial.ttf'), size*2)
    draw.text((x*2,y*2),s,fill=c,font=font,anchor='ls')
    escaped = s.replace('\\','\\\\').replace('(','\\(').replace(')','\\)')
    pdf.append(f'BT /F{2 if bold else 1} {size} Tf {rgb(c)} rg 1 0 0 1 {x} {H-y} Tm ({escaped}) Tj ET')

def arrow(x,y,direction,c):
    dx = -8 if direction == 'right' else 8
    line(x,y,x+dx,y-5,c); line(x,y,x+dx,y+5,c)

# Deliberately retain the earlier probe GPIO assignments, but remove the
# translator direction/enable pins. D0 is the ordinary byte LSB, TI CD7.
rows=[]
for b, physical in enumerate([6,7,9,10,11,12,14,15]):
    rows.append((b+4,physical,f'D{b} / CD{7-b}',17+b,
                 f'H2.{b+7}' if b<4 else f'H3.{b-3}','both',BLUE))
rows += [(12,16,'/CSR',15,'H2.5','out',TEAL),(13,17,'/CSW',14,'H2.4','out',TEAL),
         (14,19,'MODE',13,'H2.3','out',TEAL),(15,20,'MODE1',11,'H2.1','out',TEAL),
         (21,27,'/RST',34,'H4.2','out',TEAL),(18,24,'/INT',16,'H2.6','in',PURPLE),
         (19,25,'CPUCLK',38,'H4.6','in',PURPLE),(20,26,'GROMCLK',37,'H4.5','in',PURPLE),
         (None,23,'GND',12,'H2.2','ground',INK)]

rect(0,0,W,H)
text(45,54,'PICO 2  /  PICO9918 BENCH PROTOTYPE',31,bold=True)
text(45,85,'Direct bus + SWD  |  40-pin ZIF  |  Manual power  |  12 September 2026',19,c=GREY)
rect(45,106,1510,55,'#fff2d8')
text(60,130,'3.3 V data writes: confirm target U5 is HCT245. Saved schematics say HC245; direct writes then need bench validation.',17,bold=True)
text(60,152,'One USB source powers both boards through Pico 2 VBUS. GP26-GP28 are NOT 5 V tolerant; no bus wires use them.',17)

rect(45,183,995,523,'#f4f8fc')
text(65,214,'PROBE: PICO 2 HEADER',21,bold=True)
text(690,214,'TARGET: ZIF SOCKET',21,bold=True)
text(65,239,'GPIO           Physical pin',17,c=GREY)
text(690,239,'DIP pin       Target header',17,c=GREY)
for i,(gp,physical,signal,dip,header,direction,c) in enumerate(rows):
    y=269+25*i
    text(65,y+5,'GND' if gp is None else f'GP{gp}',18,bold=True)
    text(217,y+5,str(physical),18)
    line(282,y,665,y,c)
    if direction in ('out','both'): arrow(665,y,'right',c)
    if direction in ('in','both'): arrow(282,y,'left',c)
    rect(402,y-12,158,24,'#f4f8fc')
    text(416,y+5,signal,17,c,bold=True)
    text(698,y+5,str(dip),18,bold=True)
    text(826,y+5,header,18,c=GREY)
text(65,694,'Arrows show signal direction; data pins switch between input and output in firmware.',15,c=GREY)

# Physical DIP orientation, separate from the logical connection diagram.
rect(1060,183,495,680,'#f4f8fc')
text(1080,214,'ZIF: TOP / INSERTION VIEW',21,bold=True)
text(1080,242,'Align target pin 1 with socket pin 1.',17)
rect(1185,274,245,515,'#ffffff',GREY)
line(1280,274,1280,286,GREY);line(1280,286,1335,286,GREY);line(1335,286,1335,274,GREY)
socket = {r[3]:r[2].split(' / ')[0] for r in rows}
socket[33]='+5V*'
for i in range(20):
    y=300+i*24
    for p,x,d in [(i+1,1185,'left'),(40-i,1430,'right')]:
        active = p in socket
        c = INK if active else '#b2bdc6'
        signal = socket.get(p,'-')
        if d=='left':
            line(x-17,y,x,y,c)
            text(x-48,y+5,str(p),15,c,bold=active)
            text(x+9,y+5,signal,14,c)
        else:
            line(x,y,x+17,y,c)
            text(x+22,y+5,str(p),15,c,bold=active)
            text(x-93,y+5,signal,14,c)
text(1080,819,'* Pin 33: Pico VBUS via jumper or page-2 switch.',16)
text(1080,844,'15.24 mm target rows; socket body not to scale.',16,c=GREY)

rect(45,724,995,140,'#f6f3fa')
text(65,755,'SWD: SEPARATE TARGET DEBUG CONNECTOR',20,bold=True)
for i,(left,right,signal,direction) in enumerate([
    ('GP2  /  pin 4','DBG1 pin 1','SWCLK','out'),
    ('GND  /  pin 3','DBG1 pin 2','GND','ground'),
    ('GP3  /  pin 5','DBG1 pin 3','SWDIO','both')]):
    y=781+i*29
    text(65,y+5,left,17)
    line(282,y,665,y,PURPLE)
    if direction in ('out','both'):arrow(665,y,'right',PURPLE)
    if direction=='both':arrow(282,y,'left',PURPLE)
    rect(402,y-12,158,24,'#f6f3fa');text(416,y+5,signal,17,PURPLE,bold=True)
    text(690,y+5,right,17)

rect(45,880,1510,100,'#edf6f1')
text(65,909,'POWER + VIDEO',20,bold=True)
text(65,936,'PC USB -> Pico 2 USB; VBUS pin 40 -> removable jumper -> ZIF pin 33. Leave target USB unplugged.',18)
text(65,963,'Target video FFC -> existing VGA adapter -> monitor. No video signals go through the probe.',18)
text(45,1010,'1. Configure /CSR, /CSW, MODE, MODE1 and /RST idle HIGH, then fit the target power jumper.',17)
text(45,1037,'2. Make GP4-GP11 inputs BEFORE asserting /CSR. Release /CSR and allow bus turnaround before driving data.',17)
text(45,1064,'3. For a manual power cycle, release bus/SWD outputs and pulls, remove the power jumper, then reconnect.',17)
text(45,1096,'Manual option shown. Optional transistor power switch: see page 2. Use GP2/GP3, not the probe native SWD pads.',16,c=GREY)

(HERE/'wiring.svg').write_text('\n'.join(svg+['</svg>']),encoding='utf-8')
im.save(HERE/'wiring.png')

# Minimal vector PDF with standard embedded resource references; no CAD or
# external rendering dependency. A3 landscape media with the same drawing.
stream=('\n'.join(pdf+['Q'])+'\n').encode('ascii')
objects=[b'<< /Type /Catalog /Pages 2 0 R >>',
         b'<< /Type /Pages /Kids [3 0 R] /Count 1 >>',
         b'<< /Type /Page /Parent 2 0 R /MediaBox [0 0 1190.55 841.89] /Resources << /Font << /F1 4 0 R /F2 5 0 R >> >> /Contents 6 0 R >>',
         b'<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>',
         b'<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold >>',
         f'<< /Length {len(stream)} >>\nstream\n'.encode()+stream+b'endstream']
data=bytearray(b'%PDF-1.4\n%\xe2\xe3\xcf\xd3\n');offsets=[0]
for i,obj in enumerate(objects,1):
    offsets.append(len(data));data+=f'{i} 0 obj\n'.encode()+obj+b'\nendobj\n'
xref=len(data)
data+=f'xref\n0 {len(objects)+1}\n0000000000 65535 f \n'.encode()
for offset in offsets[1:]:data+=f'{offset:010} 00000 n \n'.encode()
data+=f'trailer\n<< /Size {len(objects)+1} /Root 1 0 R >>\nstartxref\n{xref}\n%%EOF\n'.encode()
(HERE/'wiring.pdf').write_bytes(data)
with (HERE/'wiring.csv').open('w',newline='',encoding='utf-8-sig') as f:
    w=csv.writer(f);w.writerow(['Probe GPIO','Pico 2 physical pin','Signal','Target connector','Target pin','Target header','Direction from probe'])
    for gp,physical,signal,dip,header,direction,_ in rows:
        w.writerow(['GND' if gp is None else f'GP{gp}',physical,signal,'ZIF',dip,header,direction])
    for gp,physical,signal,pin,direction in [('GP2',4,'SWCLK',1,'out'),('GND',3,'GND',2,'ground'),('GP3',5,'SWDIO',3,'both')]:
        w.writerow([gp,physical,signal,'DBG1',pin,'',direction])
    w.writerow(['VBUS',40,'+5V via removable jumper','ZIF',33,'H4.1','power (manual option only)'])
print('Wrote wiring.pdf, wiring.svg, wiring.png and wiring.csv')

# Keep a copy of the first page before starting the optional power drawing.
def save_page(stem):
    (HERE/(stem+'.svg')).write_text('\n'.join(svg+['</svg>']),encoding='utf-8')
    im.save(HERE/(stem+'.png'))
    stream=('\n'.join(pdf+['Q'])+'\n').encode('ascii')
    objects[-1]=f'<< /Length {len(stream)} >>\nstream\n'.encode()+stream+b'endstream'
    data=bytearray(b'%PDF-1.4\n%\xe2\xe3\xcf\xd3\n');offsets=[0]
    for i,obj in enumerate(objects,1):
        offsets.append(len(data));data+=f'{i} 0 obj\n'.encode()+obj+b'\nendobj\n'
    xref=len(data)
    data+=f'xref\n0 {len(objects)+1}\n0000000000 65535 f \n'.encode()
    for offset in offsets[1:]:data+=f'{offset:010} 00000 n \n'.encode()
    data+=f'trailer\n<< /Size {len(objects)+1} /Root 1 0 R >>\nstartxref\n{xref}\n%%EOF\n'.encode()
    (HERE/(stem+'.pdf')).write_bytes(data)

svg=[f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}">',
     '<title>Optional transistor target power switch</title>']
im=Image.new('RGB',(W*2,H*2),'white');draw=ImageDraw.Draw(im)
pdf=['q 0.744094 0 0 0.744094 0 0 cm']
rect(0,0,W,H)
text(45,54,'OPTIONAL TARGET POWER  /  PARTS ALREADY ON HAND',31,bold=True)
text(45,85,'BC327-40 + 2N3904 + four resistors  |  GP22 HIGH = power ON  |  LOW / floating = OFF',19,c=GREY)
rect(45,106,1510,55,'#fff2d8')
text(60,131,'Replace the page-1 power jumper with this switch. Leave target USB unplugged; retain all signal/ground wires.',17,bold=True)
text(60,153,'Q1 switches +5 V. Ground remains permanently connected. No target 3.3 V connection.',17)

text(95,232,'PICO 2 VBUS',22,bold=True)
text(95,261,'Physical pin 40',18)
line(95,285,740,285,TEAL,3)
text(400,267,'USB +5 V',18,TEAL)
# PNP, rotated to keep emitter at the incoming +5 V and collector at output.
# The emitter arrow points IN toward the horizontal base bar.
text(752,238,'Q1  BC327-40  (PNP)',21,bold=True)
line(740,285,800,285,TEAL,3)
line(800,285,822,320,TEAL,3)
line(858,320,880,285,TEAL,3)
line(880,285,940,285,TEAL,3)
line(795,320,885,320,TEAL,4)
line(840,320,840,360,TEAL,3)
filled_arrow(818,314,22,35,TEAL)
text(744,271,'E (3)',18);text(898,271,'C (1)',18)
text(858,352,'B (2)',18)
line(940,285,1310,285,TEAL,3)
text(1060,259,'TARGET +5 V',21,bold=True)
text(1100,321,'ZIF pin 33 / H4.1',20,bold=True)
text(1080,349,'Slightly below VBUS under load',16,c=GREY)
line(840,360,840,455,TEAL)
line(600,285,600,400,TEAL)
line(600,400,690,400,TEAL)
rect(690,388,80,24,'#ffffff',TEAL)
line(770,400,840,400,TEAL)
text(660,380,'R2  10k',18,bold=True)
rect(828,455,24,60,'#ffffff',TEAL)
text(875,482,'R1  220 ohm',20,bold=True)
text(875,510,'1/4 W',17)
line(840,515,840,575,TEAL)
# NPN, collector above, emitter below, base on the left. Arrow points OUT.
line(840,575,840,605,BLUE,3)
line(840,605,800,635,BLUE,3)
line(800,620,800,700,BLUE,4)
line(740,665,800,665,BLUE,3)
line(800,685,840,715,BLUE,3)
line(840,715,840,730,BLUE,3)
filled_arrow(834,710.5,40,30,BLUE)
text(866,605,'C (3)',18)
text(740,648,'B (2)',18)
text(866,730,'E (1)',18)
text(874,663,'Q2  2N3904',21,bold=True)
text(874,691,'NPN',18,bold=True)

text(95,613,'PICO 2 GP22',22,bold=True)
text(95,642,'Physical pin 29',18)
line(95,665,400,665,BLUE)
rect(400,653,80,24,'#ffffff',BLUE)
text(402,639,'R3  1k',18,bold=True)
line(480,665,740,665,BLUE)
line(590,665,590,709,BLUE)
rect(578,709,24,60,'#ffffff',BLUE)
text(445,745,'R4  100k',19,bold=True)
line(590,769,590,810,BLUE)
line(840,730,840,810,INK)
line(95,810,1310,810,INK,3)
text(95,794,'Pico GND',20,bold=True)
text(1080,794,'ZIF pin 12 / H2.2',20,bold=True)
text(600,841,'Common ground: same as page 1',18,c=GREY)

rect(1030,402,500,252,'#f4f8fc')
text(1050,433,'TRANSISTOR LEADS',21,bold=True)
text(1050,466,'Reference TO-92 pin numbers:',18)
text(1050,499,'BC327-40:  1 = C,  2 = B,  3 = E',18)
text(1050,532,'2N3904:      1 = E,  2 = B,  3 = C',18)
text(1050,574,'The drawings show electrical terminals,',17)
text(1050,600,'not physical leg placement. Check your',17)
text(1050,626,'manufacturer/package before inserting.',17)

rect(45,880,1510,94,'#edf6f1')
text(65,909,'HOW IT WORKS',20,bold=True)
text(65,936,'GP22 drives Q2, which pulls Q1 base down through R1. Q1 then supplies the target. R2 and R4 default both transistors off.',17)
text(65,960,'R1 gives about 18 mA base drive to Q1; R3 draws about 2.5 mA from the Pico. Initial design load: up to 150 mA target current.',17)
text(45,1005,'Check the loaded voltage at ZIF pin 33 before relying on this switch: aim for at least 4.5 V with the target running.',17,bold=True)
text(45,1033,'Before GP22 LOW, release bus/SWD outputs and disable pulls. Start with a 1-second off delay; verify that the target resets.',17)
text(45,1061,'Pico USB must supply probe + target + base drive; request a 500 mA USB configuration before enabling target power.',17)
text(45,1094,'Prototype switch: no current limiting or active discharge. No relay or flyback diode is needed. See README for connections.',16,c=GREY)
save_page('power-option')

with (HERE/'power-connections.csv').open('w',newline='',encoding='utf-8-sig') as f:
    w=csv.writer(f);w.writerow(['Net','Terminals'])
    w.writerows([
        ['USB_5V','Pico pin 40; Q1 BC327 emitter (3); R2 10k end 1'],
        ['TARGET_5V','Q1 BC327 collector (1); ZIF pin 33'],
        ['PNP_BASE','Q1 BC327 base (2); R2 10k end 2; R1 220 end 1'],
        ['NPN_COLLECTOR','R1 220 end 2; Q2 2N3904 collector (3)'],
        ['POWER_EN','Pico GP22 / pin 29; R3 1k end 1'],
        ['NPN_BASE','R3 1k end 2; R4 100k end 1; Q2 2N3904 base (2)'],
        ['GND','R4 100k end 2; Q2 2N3904 emitter (1); Pico GND; ZIF pin 12; target DBG1 pin 2'],
    ])
import pypdfium2 as p
combined=p.PdfDocument.new()
for name in ['wiring.pdf','power-option.pdf']:
    doc=p.PdfDocument(HERE/name);combined.import_pages(doc);doc.close()
combined.save(HERE/'prototype-wiring.pdf');combined.close()
print('Wrote optional power drawing and combined two-page prototype-wiring.pdf')
