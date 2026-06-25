import struct, sys
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
from cryptography.hazmat.backends import default_backend

d = open('/home/dlynch/dev/mw-recomp-mp/assets/default_mp.xex','rb').read()
pe_data_off = struct.unpack('>I', d[8:12])[0]
sec_info_off = struct.unpack('>I', d[16:20])[0]
opt_count = struct.unpack('>I', d[20:24])[0]
opt={}
off=24
for i in range(opt_count):
    key=struct.unpack('>I',d[off:off+4])[0]; val=struct.unpack('>I',d[off+4:off+8])[0]
    opt[key]=val; off+=8

# file format info at opt 0x000003FF
ff = opt[0x000003FF]
ff_size = struct.unpack('>I', d[ff:ff+4])[0]
encryption_type = struct.unpack('>H', d[ff+4:ff+6])[0]
compression_type = struct.unpack('>H', d[ff+6:ff+8])[0]
print('file format size', ff_size, 'encryption', encryption_type, 'compression', compression_type)
# compression: 0=none,1=raw(basic),2=compressed(lzx)
# encryption: 0=none,1=encrypted

# image_info file_key
ii = sec_info_off + 8 + 256
file_key = d[ii+72:ii+88]

# Retail key (XEX2)
RETAIL_KEY = bytes.fromhex('20B185A59D28FDC340583FBB0896BF91')
DEVKIT_KEY = bytes(16)

def aes_dec(key, data, iv=bytes(16)):
    c=Cipher(algorithms.AES(key), modes.CBC(iv), backend=default_backend())
    de=c.decryptor()
    return de.update(data)+de.finalize()

# decrypt session key
for name,lk in (('retail',RETAIL_KEY),('devkit',DEVKIT_KEY)):
    sk = aes_dec(lk, file_key)
    print(name,'session key', sk.hex())

session_key = aes_dec(RETAIL_KEY, file_key)

basefile = d[pe_data_off:]
print('basefile len', hex(len(basefile)))

if encryption_type==1:
    dec = aes_dec(session_key, basefile)
else:
    dec = basefile

print('first 16 decrypted', dec[:16].hex())
# if compression_type==1 (basic), there's a block table at opt 0x000003FF after the 8-byte header:
# series of (data_size, zero_size) pairs
if compression_type==1:
    # basic compression: blocks of (raw_size, padding_size)
    nblocks = (ff_size - 8)//8
    print('basic compression, nblocks', nblocks)
    out=bytearray()
    p=0
    for i in range(nblocks):
        raw = struct.unpack('>I', d[ff+8+i*8:ff+8+i*8+4])[0]
        pad = struct.unpack('>I', d[ff+8+i*8+4:ff+8+i*8+8])[0]
        out += dec[p:p+raw]
        out += bytes(pad)
        p += raw
    image = bytes(out)
elif compression_type==0:
    image = dec
else:
    print('LZX compressed - need lzx decompress (not handled)')
    image = dec

open('/tmp/claude-1000/-home-dlynch-dev-mw-recomp-mp/393c7418-d191-49f5-8fba-7ed1a10122a5/scratchpad/mp_image.bin','wb').write(image)
print('image written, len', hex(len(image)))
print('image first bytes', image[:8].hex())
