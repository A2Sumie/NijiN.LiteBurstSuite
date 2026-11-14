#define main nijin_cli_main
#include "../heraw_sr2x.cpp"
#undef main
#include <cassert>

int main(int argc, char** argv) {
    assert(argc == 2);
    assert(heraw_plan::detect_sensor_olpf_profile("Canon","EOS R1",6000,4000,false)=="gauss16");
    assert(heraw_plan::detect_sensor_olpf_profile("Canon","EOS R10",6000,4000,false)!="gauss16");
    assert(heraw_plan::detect_sensor_olpf_profile("Canon","EOS R100",6000,4000,false)!="gauss16");
    UnpackedFrame frame;
    frame.path = argv[1];
    unpack_one_frame(&frame);
    assert(frame.status == 0);
    LibRaw source;
    assert(source.open_file(argv[1]) == LIBRAW_SUCCESS);
    assert(source.unpack() == LIBRAW_SUCCESS);
    const auto& d = source.imgdata;
    assert(frame.is_xtrans == (d.idata.filters == 9));
    if (frame.is_xtrans) {
        int green = 0, red = 0, blue = 0;
        unsigned absolute_colors=0;
        for(const auto& row:d.idata.xtrans_abs) for(unsigned char c:row)
            if(c<3) absolute_colors|=1u<<c;
        for (int y = 0; y < 6; ++y)
            for (int x = 0; x < 6; ++x) {
                const int sy = y + d.sizes.top_margin, sx = x + d.sizes.left_margin;
                int color = absolute_colors==7 ? d.idata.xtrans_abs[sy % 6][sx % 6] : source.COLOR(y,x);
                assert(frame.cfa6x6[y][x] == color);
                green += color == 1; red += color == 0; blue += color == 2;
                assert(frame.bayer[(size_t)y * frame.W + x] ==
                       d.rawdata.raw_image[(size_t)sy * d.sizes.raw_width + sx]);
                float expected = d.color.black + d.color.cblack[color] +
                    d.color.cblack[6 + (sy % 6)*6 + sx % 6];
                assert(frame.black_6x6[y][x] == expected);
            }
        assert(green == 20 && red == 8 && blue == 8);
    } else {
        assert(frame.cfa6x6[0][0] == 0 && frame.cfa6x6[1][1] == 2);
        assert(frame.cfa6x6[0][1] == 1 && frame.cfa6x6[1][0] == 1);
    }
    std::vector<float> flat((size_t)frame.W * 8);
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < frame.W; ++x)
            flat[(size_t)y*frame.W+x] = 0.1f * (frame.cfa6x6[y%6][x%6] + 1);
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 12; ++x)
            for (int c = 0; c < 3; ++c)
                assert(std::fabs(sample_cfa_channel(flat.data(), 8, frame.W, y, x, c, frame.cfa6x6) -
                                 0.1f*(c+1)) < 1e-6f);
    std::printf("CFA metadata PASS: %s %dx%d CFA=%s black=%.0f AF=(%.4f,%.4f)\n",
        frame.model.c_str(), frame.W, frame.H, frame.is_xtrans ? "xtrans" : "bayer",
        frame.black_6x6[0][0], frame.af_norm_x, frame.af_norm_y);
}
