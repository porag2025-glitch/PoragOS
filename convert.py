from PIL import Image

# আপনার লোগো ফাইলের নাম এখানে দিন (যেমন: rose.png বা rose.gif)
# যদি এটি অ্যানিমেটেড GIF হয়, তবে প্রথম ফ্রেমটি নেওয়ার জন্য .seek(0) ব্যবহার করা হয়েছে
image_path = "rose.png" 

try:
    img = Image.open(image_path)
    # যদি GIF ফাইল হয়, তবে প্রথম ফ্রেমটি বেছে নিন
    if getattr(img, "is_animated", False):
        img.seek(0)
    
    # লোগোর সাইজ আপনার পছন্দমতো সেট করুন (যেমন: ১২০x১২০ পিক্সেল)
    target_width = 120
    target_height = 120
    img = img.resize((target_width, target_height)).convert("RGBA")
    pixels = img.load()

    # logo.h ফাইল তৈরি করা
    with open("logo.h", "w") as f:
        f.write("/* Auto-generated PoragOS Logo Header */\n")
        f.write("#ifndef LOGO_H\n#define LOGO_H\n\n")
        f.write(f"#define LOGO_WIDTH  {target_width}\n")
        f.write(f"#define LOGO_HEIGHT {target_height}\n\n")
        f.write(f"static const unsigned int rose_logo_pixels[{target_width} * {target_height}] = {{\n")
        
        for y in range(target_height):
            for x in range(target_width):
                r, g, b, a = pixels[x, y]
                # RGBA রঙকে 32-বিট ARGB হেক্স ফরম্যাটে রূপান্তর (0xAARRGGBB)
                hex_val = (a << 24) | (r << 16) | (g << 8) | b
                f.write(f"0x{hex_val:08X}, ")
            f.write("\n")
        
        f.write("};\n\n#endif /* LOGO_H */\n")

    print("সফলভাবে 'logo.h' ফাইল তৈরি হয়ে গেছে!")

except Exception as e:
    print(f"ত্রুটি ঘটেছে: {e}")
