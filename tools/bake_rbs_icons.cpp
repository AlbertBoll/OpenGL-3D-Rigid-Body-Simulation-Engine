// Offline SVG -> DPI atlas compiler. Never linked into the editor.
// NanoSVG (zlib) and stb_image_write retain their upstream licenses.
#define NANOSVG_IMPLEMENTATION
#define NANOSVGRAST_IMPLEMENTATION
#include "vendor/nanosvg/nanosvg.h"
#include "vendor/nanosvg/nanosvgrast.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../GEngine/include/external/stb_image/stb_image_write.h"
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include <cstdio>

int main(int argc, char** argv)
{
    if (argc != 2) return 2;
    const std::filesystem::path root(argv[1]);
    constexpr std::array names{"mouse-pointer-2", "move-3d", "rotate-3d", "scaling",
        "play", "pause", "rotate-ccw", "sliders-horizontal",
        "camera", "lightbulb", "box", "trees", "component", "sun", "stage-spotlight", "eye", "eye-off", "stage-spotlight-a", "stage-spotlight-c"};
    auto* raster = nsvgCreateRasterizer();
    if (!raster) return 3;
    for (const int size : {20, 25, 30, 40, 28, 35, 42, 56, 44, 55, 66, 88, 110, 132, 176})
    {
        const int cell = size + 4, width = cell * int(names.size());
        std::vector<unsigned char> atlas(width * cell * 4, 0);
        for (int i = 0; i < int(names.size()); ++i)
        {
            std::ifstream input(root / (std::string(names[i]).starts_with("stage-spotlight")?"custom":"lucide") / (std::string(names[i]) + ".svg"));
            if (!input) return 4;
            std::string svg((std::istreambuf_iterator<char>(input)), {});
            // Lucide currentColor becomes white; ImGui supplies the state tint.
            for (auto p = svg.find("currentColor"); p != svg.npos; p = svg.find("currentColor"))
                svg.replace(p, 12, "#ffffff");
            auto* image = nsvgParse(svg.data(), "px", 96);
            if (!image || image->width != 24 || image->height != 24) return 5;
            float scale=float(size)/24,tx=0,ty=0;
            if(size>=44 && size!=56) {
                float x0=24,y0=24,x1=0,y1=0;
                for(auto* shape=image->shapes;shape;shape=shape->next) {
                    const float stroke=shape->strokeWidth*.5f;
                    x0=std::min(x0,shape->bounds[0]-stroke);y0=std::min(y0,shape->bounds[1]-stroke);
                    x1=std::max(x1,shape->bounds[2]+stroke);y1=std::max(y1,shape->bounds[3]+stroke);
                }
                scale=float(size-2)/std::max(x1-x0,y1-y0);
                tx=(size-(x1-x0)*scale)*.5f-x0*scale;ty=(size-(y1-y0)*scale)*.5f-y0*scale;
            }
            nsvgRasterize(raster, image, tx, ty, scale,
                atlas.data() + (2 * width + i * cell + 2) * 4, size, size, width * 4);
            nsvgDelete(image);
        }
        const auto file = root / ("viewport-" + std::to_string(size) + ".png");
        if (!stbi_write_png(file.string().c_str(), width, cell, 4, atlas.data(), width * 4)) return 6;
        std::printf("%s %dx%d\n", file.string().c_str(), width, cell);
    }
    nsvgDeleteRasterizer(raster);
}
