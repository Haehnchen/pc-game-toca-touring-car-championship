#include "assets/store.hpp"
#include "core/crc32.hpp"
#include "image/decode.hpp"
#include "scene/finish_camera.hpp"
#include "scene/flag_animation.hpp"
#include "scene/view.hpp"
#include "world_geometry.hpp"

#include <bit>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>

namespace {
using namespace toca;
using namespace toca::scene;
constexpr std::array<float, 16> unit{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
constexpr unsigned width = 640, height = 480;

struct Member {
    std::size_t node, mesh;
    float distance;
    std::array<float, 3> route, surface;
};

struct Probe {
    std::uint32_t track;
    std::string id;
    unsigned kind;
    std::size_t camera;
    std::vector<Member> members;
};

// kind0: authored dense overview; kind1: near group from route; kind2: exposed end from route.
// Fixed canonical selections from woodland-audit.json:20 groups,50 members,37 end candidates.
std::vector<Probe> probes() {
    return {{0, "dense-track-0", 0, 3, {{74, 26, 59.1879000f, {-675.1879000f, 2.9680000f, 79.4951000f}, {-729.3784000f, 3.0531000f, 103.2984000f}}}},
        {1, "dense-track-1", 0, 5, {{75, 12, 35.6080000f, {371.6380000f, 59.2359000f, 337.8621000f}, {405.6248000f, 59.3397000f, 348.4839000f}}}},
        {2, "dense-track-2", 0, 12, {{429, 47, 46.6525000f, {8.0998000f, -11.0660000f, -305.6192000f}, {20.4402000f, -11.0671000f, -350.6099000f}}}},
        {3, "dense-track-3", 0, 8, {{114, 37, 31.2533000f, {182.3453000f, -1.0923000f, -43.2744000f}, {151.1759000f, 0.7934000f, -44.5717000f}}}},
        {4, "dense-track-4", 0, 4, {{180, 66, 56.5093000f, {-82.5827000f, 16.5927000f, -96.0846000f}, {-33.0534000f, 17.6439000f, -68.8994000f}}}},
        {5, "dense-track-5", 0, 3, {{74, 26, 59.1879000f, {-675.1879000f, 2.9680000f, 79.4951000f}, {-729.3784000f, 3.0531000f, 103.2984000f}}}},
        {6, "dense-track-6", 0, 18, {{46, 14, 69.3173000f, {-391.3241000f, 5.0534000f, 127.7866000f}, {-401.9375000f, 5.0536000f, 196.2865000f}}}},
        {7, "dense-track-7", 0, 4, {{40, 10, 69.3961000f, {394.6215000f, -12.7842000f, -4.9110000f}, {444.9897000f, -10.7340000f, 42.7823000f}}}},
        {8, "dense-track-8", 0, 8, {{53, 17, 62.1481000f, {-596.9067000f, 17.8990000f, 139.2613000f}, {-615.7502000f, 17.8990000f, 198.4838000f}}}},
        {1, "silv-near-12", 1, 5, {{75, 12, 35.6080000f, {371.6380000f, 59.2359000f, 337.8621000f}, {405.6248000f, 59.3397000f, 348.4839000f}}}},
        {3, "bran-near-37", 1, 8, {{114, 37, 31.2533000f, {182.3453000f, -1.0923000f, -43.2744000f}, {151.1759000f, 0.7934000f, -44.5717000f}}}},
        {4, "oult-near-111", 1, 1,
            {{277, 110, 15.4280000f, {75.7608000f, 17.7815000f, -290.7029000f}, {73.7720000f, 17.7815000f, -306.0022000f}},
                {278, 111, 8.4842000f, {121.3777000f, 18.8330000f, -285.7503000f}, {123.6967000f, 19.2755000f, -293.8994000f}},
                {279, 112, 16.2615000f, {166.7495000f, 20.0207000f, -271.9126000f}, {171.5092000f, 20.0207000f, -287.4620000f}}}},
        {4, "oult-near-100", 1, 31,
            {{236, 88, 22.9116000f, {516.8484000f, 19.4200000f, -233.2006000f}, {521.5400000f, 19.4200000f, -210.7745000f}},
                {259, 99, 15.9146000f, {496.3967000f, 20.8268000f, -228.5667000f}, {492.3840000f, 19.7668000f, -243.9307000f}},
                {260, 100, 13.3106000f, {544.2296000f, 18.9373000f, -238.3205000f}, {546.5400000f, 18.9374000f, -225.2119000f}},
                {261, 101, 13.3106000f, {544.2296000f, 18.9373000f, -238.3205000f}, {546.5400000f, 18.9374000f, -225.2119000f}}}},
        {4, "oult-near-98", 1, 3,
            {{227, 86, 14.6238000f, {-15.1895000f, 17.9268000f, -211.9884000f}, {-27.9908000f, 16.5903000f, -218.9307000f}},
                {252, 98, 14.1699000f, {-3.7162000f, 18.0076000f, -233.2773000f}, {-16.0796000f, 18.0076000f, -240.2008000f}},
                {276, 109, 16.0827000f, {11.7806000f, 17.8279000f, -258.0060000f}, {-0.0221000f, 17.8279000f, -268.9307000f}}}},
        {4, "oult-near-70", 1, 30,
            {{190, 70, 14.5668000f, {620.4283000f, 22.7224000f, -73.3801000f}, {634.6765000f, 22.7224000f, -70.3503000f}},
                {214, 80, 14.5688000f, {620.5506000f, 22.7212000f, -73.9636000f}, {634.8093000f, 22.6422000f, -70.9746000f}},
                {239, 91, 31.1260000f, {637.0251000f, 22.9569000f, -150.6598000f}, {667.4150000f, 22.9569000f, -143.9308000f}}}},
        {4, "oult-near-4", 1, 22,
            {{36, 3, 20.7764000f, {13.2133000f, 11.0008000f, 315.9546000f}, {21.5404000f, 14.1585000f, 334.7256000f}},
                {37, 4, 14.8271000f, {63.7878000f, 14.6926000f, 292.5290000f}, {69.7591000f, 14.6925000f, 306.1006000f}},
                {38, 5, 25.6011000f, {116.2708000f, 19.8265000f, 276.9228000f}, {121.5403000f, 19.8265000f, 301.9757000f}}}},
        {4, "oult-near-9", 1, 26,
            {{42, 8, 16.0524000f, {408.1003000f, 21.8470000f, 286.6442000f}, {404.0908000f, 24.6255000f, 301.9375000f}},
                {43, 9, 15.3768000f, {431.0363000f, 22.2249000f, 290.7185000f}, {428.8628000f, 22.2249000f, 305.9410000f}},
                {44, 10, 25.3042000f, {467.9932000f, 22.5568000f, 288.9213000f}, {471.5403000f, 22.5569000f, 313.9757000f}},
                {66, 17, 16.5472000f, {399.8738000f, 21.7906000f, 284.4977000f}, {395.6960000f, 21.7906000f, 300.5089000f}},
                {67, 18, 22.6817000f, {417.6430000f, 21.9993000f, 288.3825000f}, {421.5403000f, 21.9993000f, 266.0381000f}}}},
        {4, "oult-near-19", 1, 27,
            {{68, 19, 15.9572000f, {534.5856000f, 21.9994000f, 250.1026000f}, {521.5400000f, 21.9994000f, 240.9132000f}},
                {92, 25, 15.9572000f, {534.5856000f, 21.9994000f, 250.1026000f}, {521.5400000f, 21.9994000f, 240.9132000f}},
                {93, 26, 28.2159000f, {561.9487000f, 22.6995000f, 176.4869000f}, {589.7900000f, 22.6995000f, 181.0694000f}}}},
        {4, "oult-near-60", 1, 29,
            {{118, 37, 22.9349000f, {574.1190000f, 21.5735000f, 101.6333000f}, {596.6025000f, 20.6664000f, 106.0694000f}},
                {141, 50, 25.3614000f, {590.4124000f, 22.4975000f, 38.3886000f}, {566.1650000f, 21.1953000f, 31.0693000f}},
                {142, 51, 20.4013000f, {592.2190000f, 22.5613000f, 32.3921000f}, {609.9664000f, 31.0277000f, 37.8293000f}},
                {165, 59, 25.3279000f, {590.4162000f, 22.4976000f, 38.3759000f}, {566.1650000f, 22.4976000f, 31.0693000f}},
                {166, 60, 17.1637000f, {594.0040000f, 22.6244000f, 26.4677000f}, {610.4380000f, 22.6244000f, 31.4191000f}}}},
        {4, "oult-near-16", 1, 24,
            {{65, 16, 17.8070000f, {351.3598000f, 22.0232000f, 269.9497000f}, {346.4753000f, 22.0232000f, 287.0736000f}},
                {89, 24, 27.5065000f, {340.0903000f, 22.1627000f, 266.9264000f}, {346.5715000f, 22.1627000f, 240.1944000f}}}},
        {4, "oult-near-36", 1, 28,
            {{116, 35, 27.8643000f, {572.1820000f, 21.5842000f, 111.4526000f}, {544.8838000f, 20.0860000f, 106.0693000f}},
                {117, 36, 18.6608000f, {568.9930000f, 21.6675000f, 128.5103000f}, {587.4775000f, 21.6675000f, 131.0694000f}}}},
        {4, "oult-near-90", 1, 35,
            {{237, 89, 21.0522000f, {640.0519000f, 22.9619000f, -164.5331000f}, {619.5713000f, 20.8646000f, -168.9307000f}},
                {238, 90, 20.9474000f, {640.0506000f, 22.9619000f, -164.5268000f}, {619.5713000f, 22.9619000f, -168.9307000f}}}},
        {4, "oult-near-14", 1, 23,
            {{39, 6, 27.5819000f, {169.3156000f, 21.4391000f, 269.9211000f}, {171.5403000f, 21.4391000f, 297.4131000f}},
                {63, 14, 22.7356000f, {194.7831000f, 21.6085000f, 267.8705000f}, {196.5403000f, 21.6086000f, 290.5381000f}},
                {64, 15, 26.1946000f, {219.5157000f, 21.5909000f, 265.9532000f}, {221.5403000f, 21.5908000f, 292.0695000f}}}},
        {4, "oult-near-0", 1, 22, {{12, 0, 30.2342000f, {-21.9749000f, 9.5686000f, 329.8174000f}, {-12.3971000f, 21.1098000f, 356.0695000f}}}},
        {6, "crof-near-33", 1, 13,
            {{141, 27, 26.1444000f, {-123.8449000f, 7.2085000f, -248.9373000f}, {-116.9688000f, 7.2088000f, -223.7134000f}},
                {156, 33, 15.3597000f, {-83.8656000f, 7.4122000f, -264.8909000f}, {-76.7823000f, 7.4121000f, -251.2620000f}},
                {157, 34, 16.0910000f, {-69.5807000f, 7.4235000f, -273.0785000f}, {-61.0177000f, 7.4235000f, -259.4552000f}}}},
        {6, "crof-near-28", 1, 21,
            {{142, 28, 17.1917000f, {19.8110000f, 6.9156000f, -222.5598000f}, {3.0625000f, 6.9818000f, -218.6822000f}},
                {143, 29, 21.6804000f, {29.4908000f, 6.6991000f, -196.4632000f}, {9.2405000f, 6.7729000f, -188.7198000f}}}},
        {6, "crof-near-23", 1, 21, {{128, 24, 23.6479000f, {3.8019000f, 6.4442000f, -105.3912000f}, {3.0313000f, 10.1232000f, -128.7384000f}}}},
        {6, "crof-near-2", 1, 5,
            {{13, 2, 23.9629000f, {369.0941000f, 1.0888000f, 345.5578000f}, {390.4185000f, 1.1566000f, 356.4887000f}},
                {29, 8, 24.0583000f, {369.5424000f, 1.0988000f, 344.5519000f}, {391.5168000f, 1.1500000f, 354.3460000f}}}},
        {6, "crof-near-26", 1, 15, {{137, 26, 32.9776000f, {-326.1767000f, 7.7493000f, -153.1725000f}, {-346.9685000f, 7.7493000f, -178.7697000f}}}},
        {4, "end-track-4-node-36", 2, 22, {{36, 3, 20.7764000f, {13.2133000f, 11.0008000f, 315.9546000f}, {21.5404000f, 14.1585000f, 334.7256000f}}}},
        {4, "end-track-4-node-37", 2, 22, {{37, 4, 20.7764000f, {13.2133000f, 11.0008000f, 315.9546000f}, {21.5404000f, 14.1585000f, 334.7256000f}}}},
        {4, "end-track-4-node-39", 2, 23, {{39, 6, 27.5819000f, {169.3156000f, 21.4391000f, 269.9211000f}, {171.5403000f, 21.4391000f, 297.4131000f}}}},
        {4, "end-track-4-node-42", 2, 26, {{42, 8, 16.5472000f, {399.8738000f, 21.7906000f, 284.4977000f}, {395.6960000f, 21.7906000f, 300.5089000f}}}},
        {4, "end-track-4-node-43", 2, 26, {{43, 9, 25.3042000f, {467.9932000f, 22.5568000f, 288.9213000f}, {471.5403000f, 22.5569000f, 313.9757000f}}}},
        {4, "end-track-4-node-44", 2, 26, {{44, 10, 25.3042000f, {467.9932000f, 22.5568000f, 288.9213000f}, {471.5403000f, 22.5569000f, 313.9757000f}}}},
        {4, "end-track-4-node-63", 2, 23, {{63, 14, 26.1946000f, {219.5157000f, 21.5909000f, 265.9532000f}, {221.5403000f, 21.5908000f, 292.0695000f}}}},
        {4, "end-track-4-node-64", 2, 23, {{64, 15, 26.1946000f, {219.5157000f, 21.5909000f, 265.9532000f}, {221.5403000f, 21.5908000f, 292.0695000f}}}},
        {4, "end-track-4-node-65", 2, 24, {{65, 16, 17.8070000f, {351.3598000f, 22.0232000f, 269.9497000f}, {346.4753000f, 22.0232000f, 287.0736000f}}}},
        {4, "end-track-4-node-66", 2, 26, {{66, 17, 16.5472000f, {399.8738000f, 21.7906000f, 284.4977000f}, {395.6960000f, 21.7906000f, 300.5089000f}}}},
        {4, "end-track-4-node-67", 2, 26, {{67, 18, 29.5065000f, {478.0690000f, 22.5545000f, 287.1813000f}, {471.5090000f, 22.5545000f, 258.4133000f}}}},
        {4, "end-track-4-node-68", 2, 27, {{68, 19, 15.9572000f, {534.5856000f, 21.9994000f, 250.1026000f}, {521.5400000f, 21.9994000f, 240.9132000f}}}},
        {4, "end-track-4-node-89", 2, 24, {{89, 24, 27.5065000f, {340.0903000f, 22.1627000f, 266.9264000f}, {346.5715000f, 22.1627000f, 240.1944000f}}}},
        {4, "end-track-4-node-92", 2, 27, {{92, 25, 15.9572000f, {534.5856000f, 21.9994000f, 250.1026000f}, {521.5400000f, 21.9994000f, 240.9132000f}}}},
        {4, "end-track-4-node-93", 2, 28, {{93, 26, 28.2159000f, {561.9487000f, 22.6995000f, 176.4869000f}, {589.7900000f, 22.6995000f, 181.0694000f}}}},
        {4, "end-track-4-node-117", 2, 28, {{117, 36, 22.9170000f, {574.1188000f, 21.5735000f, 101.6343000f}, {596.6025000f, 21.5735000f, 106.0694000f}}}},
        {4, "end-track-4-node-141", 2, 29, {{141, 50, 27.8240000f, {572.1817000f, 21.5842000f, 111.4541000f}, {544.8838000f, 21.5842000f, 106.0693000f}}}},
        {4, "end-track-4-node-142", 2, 29, {{142, 51, 22.9170000f, {574.1188000f, 21.5735000f, 101.6342000f}, {596.6025000f, 21.5735000f, 106.0694000f}}}},
        {4, "end-track-4-node-166", 2, 29, {{166, 60, 17.5265000f, {607.6142000f, 22.9796000f, -21.7097000f}, {624.4775000f, 23.0708000f, -16.9348000f}}}},
        {4, "end-track-4-node-190", 2, 30, {{190, 70, 17.5265000f, {607.6142000f, 22.9796000f, -21.7097000f}, {624.4775000f, 23.0708000f, -16.9348000f}}}},
        {4, "end-track-4-node-214", 2, 30, {{214, 80, 31.1260000f, {637.0251000f, 22.9569000f, -150.6598000f}, {667.4150000f, 22.9569000f, -143.9308000f}}}},
        {4, "end-track-4-node-236", 2, 31, {{236, 88, 22.9116000f, {516.8484000f, 19.4200000f, -233.2006000f}, {521.5400000f, 19.4200000f, -210.7745000f}}}},
        {4, "end-track-4-node-239", 2, 35, {{239, 91, 31.1260000f, {637.0251000f, 22.9569000f, -150.6598000f}, {667.4150000f, 22.9569000f, -143.9308000f}}}},
        {4, "end-track-4-node-252", 2, 3, {{252, 98, 16.0827000f, {11.7806000f, 17.8279000f, -258.0060000f}, {-0.0221000f, 17.8279000f, -268.9307000f}}}},
        {4, "end-track-4-node-260", 2, 31, {{260, 100, 13.3106000f, {544.2296000f, 18.9373000f, -238.3205000f}, {546.5400000f, 18.9374000f, -225.2119000f}}}},
        {4, "end-track-4-node-261", 2, 31, {{261, 101, 13.3106000f, {544.2296000f, 18.9373000f, -238.3205000f}, {546.5400000f, 18.9374000f, -225.2119000f}}}},
        {4, "end-track-4-node-276", 2, 2, {{276, 109, 16.0827000f, {11.7806000f, 17.8279000f, -258.0060000f}, {-0.0221000f, 17.8279000f, -268.9307000f}}}},
        {4, "end-track-4-node-277", 2, 2, {{277, 110, 17.1520000f, {95.4187000f, 18.2019000f, -291.8467000f}, {96.5404000f, 18.2019000f, -308.9620000f}}}},
        {4, "end-track-4-node-278", 2, 1, {{278, 111, 17.1520000f, {95.4187000f, 18.2019000f, -291.8467000f}, {96.5404000f, 18.2019000f, -308.9620000f}}}},
        {6, "end-track-6-node-13", 2, 5, {{13, 2, 24.0583000f, {369.5424000f, 1.0988000f, 344.5519000f}, {391.5168000f, 1.1500000f, 354.3460000f}}}},
        {6, "end-track-6-node-29", 2, 5, {{29, 8, 24.0583000f, {369.5424000f, 1.0988000f, 344.5519000f}, {391.5168000f, 1.1500000f, 354.3460000f}}}},
        {6, "end-track-6-node-128", 2, 21, {{128, 24, 26.4671000f, {39.4837000f, 6.3897000f, -162.8783000f}, {13.3520000f, 6.5279000f, -158.6807000f}}}},
        {6, "end-track-6-node-141", 2, 14, {{141, 27, 26.1444000f, {-123.8449000f, 7.2085000f, -248.9373000f}, {-116.9688000f, 7.2088000f, -223.7134000f}}}},
        {6, "end-track-6-node-142", 2, 21, {{142, 28, 17.1917000f, {19.8110000f, 6.9156000f, -222.5598000f}, {3.0625000f, 6.9818000f, -218.6822000f}}}},
        {6, "end-track-6-node-143", 2, 21, {{143, 29, 26.4671000f, {39.4837000f, 6.3897000f, -162.8783000f}, {13.3520000f, 6.5279000f, -158.6807000f}}}},
        {6, "end-track-6-node-156", 2, 13, {{156, 33, 16.0910000f, {-69.5807000f, 7.4235000f, -273.0785000f}, {-61.0177000f, 7.4235000f, -259.4552000f}}}},
        {6, "end-track-6-node-157", 2, 13, {{157, 34, 16.0910000f, {-69.5807000f, 7.4235000f, -273.0785000f}, {-61.0177000f, 7.4235000f, -259.4552000f}}}}};
}

void expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::vector<std::array<float, 29>> triangle_attributes(const ModelMesh& mesh) {
    // GLB compaction changes index/storage order, not exact triangle attributes.
    std::vector<std::array<float, 29>> result;
    for (const auto& face : mesh.faces) {
        std::array<float, 29> triangle{};
        unsigned cursor = 0;
        for (const auto index : face.indices) {
            const auto& vertex = mesh.vertices[index];
            for (const auto value : vertex.position) {
                triangle[cursor++] = value;
            }
            for (const auto value : vertex.normal) {
                triangle[cursor++] = value;
            }
            triangle[cursor++] = vertex.u;
            triangle[cursor++] = vertex.v;
        }
        triangle[cursor++] = float(face.texture);
        for (const auto color : face.color) {
            triangle[cursor++] = float(color);
        }
        result.push_back(triangle);
    }
    std::ranges::sort(result);
    return result;
}

bool same_mesh(const ModelMesh& a, const ModelMesh& b) {
    return a.name == b.name && a.faces.size() == b.faces.size() && triangle_attributes(a) == triangle_attributes(b);
}

std::uint32_t geometry_crc32(const ModelMesh& mesh) {
    std::vector<std::uint8_t> bytes;
    for (const auto& triangle : triangle_attributes(mesh)) {
        for (const auto value : triangle) {
            // Canonical little-endian binary32; numeric equality treats signed
            // zero alike, so normalize it before computing the stable CRC32.
            const auto bits = std::bit_cast<std::uint32_t>(value == 0.f ? 0.f : value);
            for (unsigned shift = 0; shift < 32; shift += 8) {
                bytes.push_back(std::uint8_t(bits >> shift));
            }
        }
    }
    return crc32(bytes);
}

std::string checksum(std::span<const std::uint8_t> bytes) {
    std::ostringstream s;
    s << std::hex << std::setw(8) << std::setfill('0') << crc32(bytes);
    return s.str();
}

void verify(const SceneResources& original, const SceneResources& mega, const std::filesystem::path& directory, unsigned track) {
    const auto& a = original.trees;
    const auto& b = mega.trees;
    expect(a.nodes.size() == b.nodes.size() && a.meshes.size() == b.meshes.size() && a.textures.size() == b.textures.size(), "Woodland bundle must retain every source slot");
    for (std::size_t i = 0; i < a.nodes.size(); ++i) {
        const auto& x = a.nodes[i];
        const auto& y = b.nodes[i];
        expect(x.parent == y.parent && x.transform == y.transform && x.id_flags == y.id_flags && x.variants == y.variants, "Woodland source node registration changed");
    }
    std::map<std::size_t, std::uint32_t> authored;
    if (track == 4) {
        authored[14] = 0xbff44486u;
        authored[15] = 0x7fddda90u;
        authored[100] = 0x76c0c3f7u;
        authored[101] = 0x1178153eu;
    }
    if (track == 6) {
        authored[23] = 0xaef22678u;
        authored[24] = 0x0e84d71eu;
    }
    if (track == 8) {
        authored[0] = 0x5d40f7b0u;
        authored[19] = 0x5d3853a8u;
    }
    for (std::size_t i = 0; i < a.meshes.size(); ++i) {
        if (authored.contains(i)) {
            expect(geometry_crc32(b.meshes[i]) == authored.at(i), "Accepted woodland pilot changed");
        } else {
            expect(same_mesh(a.meshes[i], b.meshes[i]), "Unconverted woodland must retain exact source triangle attributes");
        }
    }
    expect(original.trees_baked.chunks.size() == mega.trees_baked.chunks.size(), "Baked woodland source slots changed");
    for (std::size_t i = 0; i < original.trees_baked.chunks.size(); ++i) {
        const auto& x = original.trees_baked.chunks[i];
        const auto& y = mega.trees_baked.chunks[i];
        expect(x.node_index == y.node_index && x.variant_index == y.variant_index && x.origin_xz == y.origin_xz, "Baked woodland source registration changed");
        const bool pilot = (track == 4 && (x.node_index == 63 || x.node_index == 64 || x.node_index == 260 || x.node_index == 261))
                           || (track == 6 && (x.node_index == 127 || x.node_index == 128)) || (track == 8 && (x.node_index == 6 || x.node_index == 60));
        if (!pilot) {
            expect(same_mesh(x.mesh, y.mesh), "Unconverted baked woodland changed");
        }
    }
    for (std::size_t i = 0; i < a.textures.size(); ++i) {
        const auto& x = a.textures[i];
        const auto& y = b.textures[i];
        expect(x.name == y.name && y.encoding == ImageEncoding::webp, "Woodland must load authored WebP for every source material");
        const auto path = directory / "scene" / (checksum(x.bmp) + ".webp");
        std::ifstream f(path, std::ios::binary);
        const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(f), {}};
        expect(!bytes.empty() && bytes == y.bmp, "Woodland must load exact source-CRC bundle bytes");
        auto pixels = toca::decode_model_texture(y);
        expect(pixels->w <= 512 && pixels->h <= 512 && bytes.size() <= 256 * 1024, "Woodland WebP exceeds accepted budget");
        std::cout << "MATERIAL track=" << track << " slot=" << i << " name=" << y.name << " source=" << checksum(x.bmp) << " output=" << checksum(bytes) << '\n';
    }
    expect(original.collision_hpc == mega.collision_hpc && original.route_points == mega.route_points && original.ground.sector_centers == mega.ground.sector_centers,
        "Woodland presentation must retain each track's own ground, collision, route and sectors");
}

std::size_t chunk_index(const SceneResources& scene, const Member& member) {
    const auto& chunks = scene.trees_baked.chunks;
    const auto p = std::ranges::find(chunks, member.node, &TerrainChunk::node_index);
    expect(p != chunks.end() && p->mesh.name == scene.trees.meshes.at(member.mesh).name, "Audit selection does not match canonical baked woodland binding");
    return std::size_t(p - chunks.begin());
}

std::array<float, 3> center(const ModelMesh& mesh) {
    std::array<float, 3> lo{32768, 32768, 32768}, hi{-32768, -32768, -32768};
    for (const auto& v : mesh.vertices) {
        for (unsigned a = 0; a < 3; ++a) {
            lo[a] = std::min(lo[a], v.position[a]);
            hi[a] = std::max(hi[a], v.position[a]);
        }
    }
    return {(lo[0] + hi[0]) * .5f, (lo[1] + hi[1]) * .5f, (lo[2] + hi[2]) * .5f};
}

CameraInputs camera(const SceneResources& original, const Probe& probe, bool fallback = false) {
    const auto& member = probe.members.front();
    const auto& mesh = original.trees_baked.chunks[chunk_index(original, member)].mesh;
    auto target = center(mesh);
    std::array<SceneCamera, 1> origins{};
    if (probe.kind == 0 && !fallback) {
        expect(probe.camera < original.cameras.size(), "Dense overview needs actual authored camera");
        origins[0] = original.cameras[probe.camera];
    } else {
        target[0] = member.surface[0];
        target[2] = member.surface[2];
        // Aim at the actual carrier near the route witness; keep the camera at
        // source road height plus eye height, rather than moving the forest.
        for (unsigned a = 0; a < 3; ++a) {
            origins[0].transform[12 + a] = std::int32_t((member.route[a] + (a == 1 ? 1.2f : 0)) * 65536);
        }
    }
    std::array<std::int32_t, 3> q{};
    for (unsigned a = 0; a < 3; ++a) {
        q[a] = std::int32_t(target[a] * 65536);
    }
    const FinishCamera look(origins);
    return {look.world_to_view(q), default_projection(4, width, height), look.position()};
}

SceneResources mode(const SceneResources& original, const SceneResources& mega, unsigned selected) {
    auto scene = mega;
    if (selected < 2) {
        scene.trees = original.trees;
        scene.trees_baked = original.trees_baked;
        if (selected == 1) {
            scene.trees.textures = mega.trees.textures;
            scene.trees_baked.textures = mega.trees_baked.textures;
        }
    }
    return scene;
}

std::vector<std::uint32_t> render(gpu::Renderer& renderer, const SceneResources& scene, const CameraInputs& camera, const std::string& file = {}) {
    SceneView view(renderer, scene);
    SourceRandom random(7);
    FlagAnimation flags(scene.flags, random);
    HorizonActors actors(scene.moving_horizon);
    auto frame = view.frame(camera, scene_lighting(scene.setup), {unit}, flags.variants(), actors.frame());
    expect(!toca::test::materialize_world(frame).indices.empty(), "Overview scene must reach actual GPU geometry");
    expect(renderer.draw(frame, false), "Overview GPU target unavailable");
    if (!file.empty()) {
        renderer.save_screenshot(file);
    }
    return renderer.readback();
}

std::vector<std::uint32_t> without(gpu::Renderer& renderer, const SceneResources& mega, const CameraInputs& cam, std::optional<std::size_t> node) {
    auto scene = mega;
    if (node) {
        scene.trees.nodes.at(*node).variants.clear();
        std::erase_if(scene.trees_baked.chunks, [&](const auto& c) {
            return c.node_index == *node;
        });
    } else {
        for (auto& n : scene.trees.nodes) {
            n.variants.clear();
        }
        scene.trees_baked.chunks.clear();
    }
    return render(renderer, scene, cam);
}

std::size_t difference(const std::vector<std::uint32_t>& a, const std::vector<std::uint32_t>& b) {
    expect(a.size() == width * height && a.size() == b.size(), "Overview readback size mismatch");
    std::size_t result = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        result += a[i] != b[i];
    }
    return result;
}

void probe(gpu::Renderer& renderer, const SceneResources& original, const SceneResources& mega, const Probe& selected, std::ofstream& csv, bool fallback = false) {
    const auto cam = camera(original, selected, fallback);
    const auto prefix = "woodland-overview-" + selected.id;
    const auto source = render(renderer, mode(original, mega, 0), cam, prefix + "-original.png");
    const auto full = render(renderer, mega, cam, prefix + "-mega.png");
    const auto artwork = difference(source, full);
    if (selected.kind == 0 && (selected.track == 6 || selected.track == 8)) {
        render(renderer, mode(original, mega, 1), cam, prefix + "-ai-original-geometry.png");
    }
    const auto empty = without(renderer, mega, cam, std::nullopt);
    const auto all_coverage = difference(full, empty);
    if (selected.kind == 0) {
        expect(all_coverage > 20 && artwork > 20, "Dense overview must show actual authored woodland pixels, not only an upload");
    }
    for (const auto& member : selected.members) {
        chunk_index(original, member);
        const auto coverage = difference(full, without(renderer, mega, cam, member.node));
        csv << selected.id << ',' << selected.kind << ',' << selected.track << ',' << member.node << ',' << member.mesh << ',' << original.trees.meshes[member.mesh].name << ','
            << member.distance << ',' << coverage << ',' << all_coverage << ',' << artwork << ',' << (coverage ? "visible" : "not-visible") << '\n';
        std::cout << "COVERAGE " << selected.id << " track=" << selected.track << " node=" << member.node << " mesh=" << member.mesh << " distance=" << member.distance
                  << " pixels=" << coverage << '\n';
        if (coverage < 20 && selected.kind == 1 && !fallback) {
            // A group view is not evidence for a member behind its camera or
            // neighbors. Capture its own route-witness view and report zero
            // honestly if source backface culling/occlusion still hides it.
            Probe extra{selected.track, selected.id + "-node-" + std::to_string(member.node), 1, selected.camera, {member}};
            probe(renderer, original, mega, extra, csv, true);
        }
    }
}

void reuse(gpu::Renderer& renderer, const Terrain& gp, const Terrain& short_track, const CameraInputs& cam, const Lighting& light) {
    expect(gp.chunks.size() == short_track.chunks.size() && gp.textures.size() == short_track.textures.size(), "Donington reuse family slots differ");
    for (std::size_t i = 0; i < gp.chunks.size(); ++i) {
        const auto& a = gp.chunks[i];
        const auto& b = short_track.chunks[i];
        expect(a.node_index == b.node_index && a.variant_index == b.variant_index && a.origin_xz == b.origin_xz && same_mesh(a.mesh, b.mesh),
            "Donington baked woodland must be exactly reused");
    }
    for (std::size_t i = 0; i < gp.textures.size(); ++i) {
        expect(gp.textures[i].bmp == short_track.textures[i].bmp, "Donington reused woodland material bytes differ");
    }
    const auto draw = [&](const Terrain& terrain, const char* file) {
        TerrainView view(renderer, terrain);
        auto frame = view.frame(cam, light);
        expect(toca::test::materialize_world(frame).indices.size() > 3, "Donington isolated reuse view must show actual woodland");
        expect(renderer.draw(frame, false), "Donington reuse GPU target unavailable");
        renderer.save_screenshot(file);
        return renderer.readback();
    };
    const auto a = draw(gp, "woodland-overview-donington-gp-reuse.png"), b = draw(short_track, "woodland-overview-donington-short-reuse.png");
    expect(a == b, "Identical Donington TREES at same world camera/light must produce identical GPU pixels");
    std::cout << "PASS Donington isolated TREES reuse; routes and GROUND intentionally remain distinct\n";
}
} // namespace

int main(int argc, char** argv) {
    try {
        expect(argc == 3, "Usage: toca-woodland-overview-tests CANONICAL_ROOT MEGA_ROOT");
        gpu::check(SDL_Init(SDL_INIT_VIDEO), "Initialize isolated woodland overview");

        struct Quit {
            ~Quit() {
                SDL_Quit();
            }
        } quit;

        std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("TOCA woodland overview", width, height, 0), SDL_DestroyWindow);
        gpu::check(bool(window), "Create overview GPU window");
        gpu::Renderer renderer(window.get());
        const AssetStore store(argv[1]);
        const auto selections = probes();
        std::ofstream csv("woodland-overview.csv");
        csv << "probe,kind,track,node,mesh,name,route_distance_m,visible_member_pixels,visible_woodland_pixels,original_mega_pixels,status\n";
        std::optional<Terrain> gp;
        std::optional<CameraInputs> gp_camera;
        Lighting gp_light{};
        for (std::uint32_t track = 0; track < 9; ++track) {
            const auto original = load_scene_resources(store, resolve_setup({.track_id = track, .weather_choice = 2, .detail = 3}, 2));
            const auto mega = load_scene_resources(store, resolve_setup({.track_id = track, .weather_choice = 2, .detail = 4}, 2), argv[2]);
            verify(original, mega, argv[2], track);
            for (const auto& selection : selections) {
                if (selection.track == track) {
                    probe(renderer, original, mega, selection, csv);
                }
            }
            if (track == 0) {
                gp = mega.trees_baked;
                gp_camera = camera(original, selections.front());
                gp_light = scene_lighting(mega.setup);
            }
            if (track == 5) {
                expect(gp && gp_camera, "GP reuse baseline missing");
                reuse(renderer, *gp, mega.trees_baked, *gp_camera, gp_light);
            }
        }
        std::cout << "PASS: all9 woodland bundle contracts,20 near groups,50 member coverage records,37 exposed-end probes, Donington isolated reuse; visual review required for "
                     "zero/occluded members\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
