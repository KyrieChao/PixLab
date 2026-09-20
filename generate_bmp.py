import struct

width, height = 4, 4

# 每行字节数要 4 字节对齐
row_bytes = width * 3
row_padded = (row_bytes + 3) & ~3
pad = row_padded - row_bytes

def bgr(r, g, b):
    return bytes([b, g, r])

# 4x4 每格一个颜色（BGR）
colors = [
    (255, 0, 0),     # 红
    (0, 255, 0),     # 绿
    (0, 0, 255),     # 蓝
    (255, 255, 0),   # 黄
    (0, 255, 255),   # 青
    (255, 0, 255),   # 紫
    (255, 255, 255), # 白
    (0, 0, 0),       # 黑
]

pixel_data = b''
# BMP 默认 bottom-up：从最后一行开始存
for y in range(height - 1, -1, -1):
    for x in range(width):
        idx = (y * width + x) % len(colors)
        r, g, b = colors[idx]
        pixel_data += bgr(r, g, b)
    pixel_data += b'\x00' * pad

file_size = 14 + 40 + len(pixel_data)

file_header = b'BM' + struct.pack('<IHHI',
    file_size, 0, 0, 54)

info_header = struct.pack('<IiiHHIIiiII',
    40, width, height, 1, 24, 0,
    len(pixel_data), 0, 0, 0, 0)

with open('test.bmp', 'wb') as f:
    f.write(file_header + info_header + pixel_data)

print(f'已生成 test.bmp: {width}x{height}, 大小 {file_size} 字节')