# Read-only metadata lookup for a public Mega file link (no download).
# Calls the same public API megatools uses: {"a":"g","p":handle} returns size and encrypted attrs.
import base64, json, subprocess, sys, urllib.request, random, re

def b64d(s):
    s = s.replace('-', '+').replace('_', '/')
    return base64.b64decode(s + '=' * (-len(s) % 4))

def main(link):
    m = re.search(r'mega\.nz/file/([^#]+)#(.+)$', link)
    if not m:
        sys.exit('not a mega file link')
    handle, keystr = m.group(1), m.group(2)
    key = b64d(keystr)
    if len(key) != 32:
        sys.exit('unexpected key length %d' % len(key))
    aes = bytes(a ^ b for a, b in zip(key[:16], key[16:]))
    req = json.dumps([{"a": "g", "p": handle}]).encode()
    url = 'https://g.api.mega.co.nz/cs?id=%d' % random.randint(0, 10**9)
    r = urllib.request.Request(url, data=req, headers={'Content-Type': 'application/json'})
    resp = json.load(urllib.request.urlopen(r, timeout=60))[0]
    if isinstance(resp, int):
        sys.exit('mega api error %d' % resp)
    at = b64d(resp['at'])
    # attrs are AES-128-CBC zero-IV encrypted with the derived key; decrypt via openssl
    out = subprocess.run(['openssl', 'enc', '-d', '-aes-128-cbc', '-nopad', '-K', aes.hex(), '-iv', '0' * 32],
                         input=at, capture_output=True, check=True).stdout
    print('size_bytes:', resp['s'])
    print('size_gib:', round(resp['s'] / 2**30, 3))
    print('attrs:', out.rstrip(b'\0').decode('utf-8', 'replace'))

if __name__ == '__main__':
    main(sys.argv[1])
