"""Independent connector checks for the printable Pico 2 wiring guide."""
from pathlib import Path
import csv
import math
import xml.etree.ElementTree as ET
import pypdfium2 as p

HERE=Path(__file__).resolve().parent
with (HERE/'wiring.csv').open(encoding='utf-8-sig') as f:
    rows=list(csv.DictReader(f))
assert len(rows)==21
pico={0:1,1:2,2:4,3:5,4:6,5:7,6:9,7:10,8:11,9:12,10:14,11:15,
      12:16,13:17,14:19,15:20,16:21,17:22,18:24,19:25,20:26,21:27,22:29}
headers={'H1':[None,None],
         'H2':['MODE1','GND','MODE','/CSW','/CSR','/INT','D0 / CD7','D1 / CD6','D2 / CD5','D3 / CD4'],
         'H3':['D4 / CD3','D5 / CD2','D6 / CD1','D7 / CD0'],
         'H4':['+5V via removable jumper','/RST',None,None,'GROMCLK','CPUCLK',None,None]}
positions={**{n:(1.27+2.54*(n-1),-1.27) for n in range(1,21)},
           **{n:(1.27+2.54*(40-n),-16.51) for n in range(21,41)}}
expected={}
with (HERE.parent/'v2.0/pico9918_v2_0_picknplace.csv').open(encoding='utf-16') as f:
    for row in csv.DictReader(f,delimiter='\t'):
        ref=row['Designator']
        if ref not in headers:continue
        x=float(row['Pad X'].removesuffix('mm'));y=float(row['Pad Y'].removesuffix('mm'))
        a=math.radians(float(row['Rotation']))
        for i,net in enumerate(headers[ref]):
            if net is None:continue
            xy=(x+2.54*i*math.cos(a),y+2.54*i*math.sin(a))
            matches=[n for n,pos in positions.items() if math.dist(xy,pos)<.02]
            assert len(matches)==1
            expected[matches[0]]=(f'{ref}.{i+1}',net)
seen=set();used=set()
for row in rows:
    gp=row['Probe GPIO'];physical=int(row['Pico 2 physical pin']);pin=int(row['Target pin'])
    if gp=='VBUS':
        assert physical==40 and pin==33 and row['Target connector']=='ZIF'
        assert row['Direction from probe']=='power (manual option only)'
    elif gp=='GND':assert physical in (3,8,13,18,23,28,33,38)
    else:
        n=int(gp.removeprefix('GP'))
        assert physical==pico[n]
        assert 0<=n<=22 and n not in used
        used.add(n)
    if row['Target connector']=='ZIF':
        assert pin not in seen;seen.add(pin)
        assert (row['Target header'],row['Signal'])==expected[pin]
        if row['Signal'].startswith('D'):
            bit=int(row['Signal'][1]);assert n==bit+4 and pin==bit+17
            assert row['Direction from probe']=='both'
        elif row['Signal'] in ('/INT','CPUCLK','GROMCLK'):
            assert row['Direction from probe']=='in'
        elif gp not in ('GND','VBUS'):assert row['Direction from probe']=='out'
    else:
        assert row['Target connector']=='DBG1'
        assert (gp,row['Signal'])=={1:('GP2','SWCLK'),2:('GND','GND'),3:('GP3','SWDIO')}[pin]
assert seen==set(expected)
assert not ({26,27,28}&used)
assert 22 not in used and pico[22]==29

with (HERE/'power-connections.csv').open(encoding='utf-8-sig') as f:
    power={r['Net']:set(r['Terminals'].split('; ')) for r in csv.DictReader(f)}
assert power['USB_5V']=={'Pico pin 40','Q1 BC327 emitter (3)','R2 10k end 1'}
assert power['TARGET_5V']=={'Q1 BC327 collector (1)','ZIF pin 33'}
assert power['PNP_BASE']=={'Q1 BC327 base (2)','R2 10k end 2','R1 220 end 1'}
assert power['NPN_COLLECTOR']=={'R1 220 end 2','Q2 2N3904 collector (3)'}
assert power['POWER_EN']=={'Pico GP22 / pin 29','R3 1k end 1'}
assert power['NPN_BASE']=={'R3 1k end 2','R4 100k end 1','Q2 2N3904 base (2)'}
assert power['GND']=={'R4 100k end 2','Q2 2N3904 emitter (1)','Pico GND','ZIF pin 12','target DBG1 pin 2'}
assert len(set.union(*power.values()))==sum(map(len,power.values()))
for name in ['wiring.svg','power-option.svg']:ET.parse(HERE/name)
doc=p.PdfDocument(HERE/'prototype-wiring.pdf')
assert len(doc)==2
for i in range(2):
    page=doc[i]
    s=page.get_textpage().get_text_range()
    assert 'GP22' in s if i else 'D0 / CD7' in s
    page.render(scale=1.2).to_pil().save(HERE/f'review-page-{i+1}.png')
report='PASS: 20 bus/SWD/ground connections plus manual VBUS jumper, original target header geometry, Pico 2 physical pins, 7 optional power nets, SVG parsing, and both PDF pages.\nStatic checks only; fitted U5 and loaded transistor-switch voltage remain bench checks.\n'
(HERE/'wiring-check.txt').write_text(report)
print(report)
