from pathlib import Path
import re,urllib.request,concurrent.futures
p=Path(__file__).with_name('omarchy-touch-review.html')
s=p.read_text()
if '<!-- ICON_SYMBOLS -->' not in s:
 print('Icons already embedded; no network requests made.')
 raise SystemExit(0)
names=['house','grid-2x2','terminal','globe','folder','settings','code','calculator','file','music','search','layers','panel-left','sliders-horizontal','bell','wifi','bluetooth','sun','volume-2','power','rotate-cw','x','keyboard','chevron-right','arrow-left','arrow-left-right','check','wifi-off','triangle-alert','folder-open','save','text-select','copy','scissors','clipboard','skip-back','skip-forward','pause','play','moon','lock','shield-check','text-cursor','contrast','accessibility','more-horizontal','maximize','chevron-down','panels-top-left','plus','info','arrow-right','arrow-up','arrow-down','delete']
aliases={'music':'audio-lines','text-select':'text-cursor','more-horizontal':'ellipsis'}
def get(n):
 u='https://raw.githubusercontent.com/lucide-icons/lucide/main/icons/'+aliases.get(n,n)+'.svg'
 try:raw=urllib.request.urlopen(u,timeout=20).read().decode()
 except Exception as e:raise RuntimeError(n+' '+u+' '+str(e))
 inner=re.search(r'<svg\b[^>]*>(.*)</svg>',raw,re.S).group(1)
 return '<symbol id="i-'+n+'" viewBox="0 0 24 24">'+inner+'</symbol>'
with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool: symbols=list(pool.map(get,names))
license=urllib.request.urlopen('https://raw.githubusercontent.com/lucide-icons/lucide/main/LICENSE',timeout=20).read().decode()
svg='<svg xmlns="http://www.w3.org/2000/svg" width="0" height="0" style="position:absolute;overflow:hidden" aria-hidden="true"><!-- Lucide icon subset. '+license.replace('--','—')+' -->'+''.join(symbols)+'</svg>'
s=s.replace('<!-- ICON_SYMBOLS -->',svg)
p.write_text(s)
print('Embedded',len(names),'Lucide SVG symbols; license inline.',len(s.encode()),'bytes')
