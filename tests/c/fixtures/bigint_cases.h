/*
 * Big-integer fixtures (binomials, ratios, histogram division).
 *
 * GENERATED reference data - do not edit by hand. Frozen from the Python
 * reference at baseline commit 153b78ac025858f11c124e6ae17953fdecd0aec3 by a one-off
 * generator kept outside the repository (see fixture_types.h for the
 * method, oracles, conventions and schema). Provenance strings name the
 * baseline unittest each case ports (module.Class.method).
 *
 * Known answers for the multiprecision gate: binomial coefficients used by
 * the solver (up to C(6400,3200), 6394 bits), exact ratio-to-double
 * conversion with the endpoint guard (10^400 denominators and the 80x80
 * pool/frontier ratios), and exact low-order polynomial division of
 * histogram products. All values are little-endian uint32 limbs.
 * Port destination: tests/c/test_bigint.c.
 */
#pragma once

#include "fixture_types.h"

static const uint32_t bigint_c_0_0_limbs[1] = {
    0x00000001u,
};
static const uint32_t bigint_c_6_1_limbs[1] = {
    0x00000006u,
};
static const uint32_t bigint_c_6_2_limbs[1] = {
    0x0000000fu,
};
static const uint32_t bigint_c_14_4_limbs[1] = {
    0x000003e9u,
};
static const uint32_t bigint_c_20_7_limbs[1] = {
    0x00012ed0u,
};
static const uint32_t bigint_c_6374_2_limbs[1] = {
    0x0135eadfu,
};
static const uint32_t bigint_c_6375_3_limbs[2] = {
    0x0c8e9d13u, 0x0000000au,
};
static const uint32_t bigint_c_6400_0_limbs[1] = {
    0x00000001u,
};
static const uint32_t bigint_c_6400_1_limbs[1] = {
    0x00001900u,
};
static const uint32_t bigint_c_6400_2_limbs[1] = {
    0x01387380u,
};
static const uint32_t bigint_c_6400_6400_limbs[1] = {
    0x00000001u,
};
static const uint32_t bigint_c_6396_1999_limbs[179] = {
    0xdc6ca880u, 0x45b1e066u, 0x2a04709fu, 0xd72af73eu, 0x92b95c11u, 0x4eb7b796u,
    0xa7f69f02u, 0xec48327eu, 0xb3869cdcu, 0x1906e056u, 0x9d815d70u, 0xac082a91u,
    0xc6423e37u, 0x4558cc31u, 0xfc7399d4u, 0x91b91b48u, 0xf5845b95u, 0x56de6b4bu,
    0x3e9be468u, 0x87fb14aeu, 0x7c3939a0u, 0xc7731f82u, 0x9d472c2eu, 0x931148b6u,
    0x48aceb9au, 0xe8bfa1b8u, 0xe04f8fc8u, 0xe8f30ffdu, 0x57e20964u, 0x75ddb271u,
    0x81e12ab9u, 0x0cff0968u, 0xe54fe2fbu, 0x8dfdce0au, 0x3b3417d9u, 0xf466e477u,
    0xa46d6e1cu, 0x43c48106u, 0xc320ba77u, 0x9af16cbdu, 0xb3aad378u, 0xdacffe18u,
    0x35645d2bu, 0xe70b4c6bu, 0x913e564eu, 0xdd410303u, 0xb5af72eeu, 0x02cb5353u,
    0xa8afcd24u, 0xbee7cb91u, 0xa491f956u, 0x238c59c7u, 0x6a51e066u, 0xd27ca3cau,
    0xc757f29cu, 0xf23f68aau, 0x01bd94abu, 0xa1e0315cu, 0x1b66dc7bu, 0xc0b740cau,
    0xdf639e36u, 0x95fb7817u, 0xe88bce2du, 0xb4b4c608u, 0x05fc6958u, 0xefa402b0u,
    0x7e0a8389u, 0x9a02abd8u, 0x51ab4367u, 0x481ea49au, 0x3617b76eu, 0xc4ad8ac2u,
    0x1c0fb341u, 0xf996ae1eu, 0xc8aa2ba4u, 0x34087081u, 0x16eaa2dau, 0x255e7c4cu,
    0x1e189acbu, 0xf58d9a7cu, 0xa402ae66u, 0x111d00fau, 0x5045c7f4u, 0x9b2a04e1u,
    0x6f3ba326u, 0x10acb34fu, 0xca928f41u, 0x94c102a0u, 0xa243cf6bu, 0x643a9868u,
    0x89c476a1u, 0x41c7652eu, 0xd0b36d64u, 0x1588e5b4u, 0xeeeb15bau, 0x59947594u,
    0xddfe8321u, 0xeeaabb5du, 0x8aaba705u, 0x1bb3b166u, 0x0befd0d1u, 0x2ad6fcd2u,
    0xbd52d3ddu, 0x5a3138c3u, 0x3ad4a917u, 0x3c0da015u, 0xee0006fcu, 0x5d45e5acu,
    0xeed9a470u, 0xb4269e46u, 0x83de0bf2u, 0x5dc922f5u, 0x29306b11u, 0xaf89d076u,
    0xb5b440f0u, 0x535567f0u, 0xfbb1a6efu, 0xdb3678c0u, 0x34fe2395u, 0x3ec1adf0u,
    0x8674fcb8u, 0x3c7ed205u, 0xae8d0606u, 0xcdf162ddu, 0x8fa8fb96u, 0xfd9ff285u,
    0xd13adfaeu, 0xaf49d997u, 0xec354ca1u, 0xa3295c5fu, 0x5fe4f18cu, 0x8252ffeeu,
    0xda2c5705u, 0xf736dcc1u, 0xddd1f4a3u, 0x69d69209u, 0xcedef3e2u, 0x17da844du,
    0x394d83ddu, 0x438bd5b7u, 0x600c5bb4u, 0xe542755du, 0x27f5d5c0u, 0x13a33c51u,
    0x72259488u, 0x8d7191beu, 0x5b826154u, 0x78e9d95cu, 0x510444eau, 0x79289a49u,
    0x35122fe5u, 0x097084d3u, 0x6f4068b9u, 0xbc6b1c82u, 0x4271c72au, 0xc76efba2u,
    0xa0843e43u, 0x5f1f0a91u, 0x1895138eu, 0x09cdac29u, 0x81534b45u, 0x4b249da4u,
    0x79524008u, 0xc271d975u, 0xa84c8e1fu, 0xb32d7ef4u, 0xdddf6d45u, 0x6194ed36u,
    0x5d43ef13u, 0x64cc2a03u, 0xd7edffe5u, 0x4e413bb0u, 0x903498cau, 0x571fc170u,
    0x1014e327u, 0x599e0eabu, 0xe0ed46b7u, 0x4dea8bf0u, 0x1bfd84ccu,
};
static const uint32_t bigint_c_6400_3200_limbs[200] = {
    0x5a209468u, 0x0e697006u, 0xd9a47633u, 0x7bf25d7du, 0x7e007419u, 0xf3899541u,
    0xbb39c42eu, 0xa8e1e85cu, 0xa197ccebu, 0xdea2aafeu, 0x808b66dbu, 0x7dd30214u,
    0xc05f9a91u, 0x194515d6u, 0xea58b26fu, 0x244827dfu, 0x9bf391aau, 0x751757efu,
    0x3d0a9ab5u, 0x73432618u, 0xf2f15cbbu, 0xda90a630u, 0x8d544f72u, 0x6f840f83u,
    0xcf5c9325u, 0x990b388cu, 0x5d16ff90u, 0x2b64b61cu, 0xb62b31c8u, 0xb48ce61au,
    0x2f6b0ec9u, 0x9ce581e5u, 0xfb52c539u, 0xaa631db9u, 0x6bbe0181u, 0xda91536au,
    0x8ff10d5fu, 0x4ce55edau, 0xc3f7550du, 0xa0fb59b8u, 0xe8a412b4u, 0xe3af5ad8u,
    0x5f76d89fu, 0x8c732c37u, 0xba9acda3u, 0xdec0ae9bu, 0x68f28fdcu, 0xe5a14defu,
    0x0d33ed0fu, 0xb5ff4489u, 0xf5b8d914u, 0x923c8350u, 0x2cf32c50u, 0x1bc9f990u,
    0xb27db169u, 0x7b6cdfe0u, 0x633d50b4u, 0xa1ac60a2u, 0x43ced45au, 0xa03f0518u,
    0x958c72e6u, 0x7c55b03du, 0x26da5c2bu, 0x87758acbu, 0xce7fd960u, 0x1255cf6du,
    0x9e1ed77eu, 0x184ad264u, 0xcc50cddau, 0xd529b204u, 0x1b7bfc44u, 0x72c967fdu,
    0x3640c7ebu, 0x871bb513u, 0x273fa82eu, 0x07b3a9f1u, 0x9c20291bu, 0xb096c604u,
    0x20738bcau, 0xf4825618u, 0xf8dab5d3u, 0x5a6cbcf3u, 0x868d7b92u, 0x5a76fa1du,
    0xfd44bf24u, 0xf5b8dd3du, 0xd847ec69u, 0x4bb7d58bu, 0xe5deddf8u, 0x3ac46a68u,
    0x6e043b41u, 0xe924a22eu, 0x1202cadbu, 0x4210c802u, 0xfa406abcu, 0xd806903eu,
    0xbacc1fc8u, 0xbd5e597du, 0x340cc0f6u, 0x1635de38u, 0xe328f86du, 0x55c12e78u,
    0xc4690feau, 0xa32dcc3au, 0xf12348dfu, 0x624afba8u, 0xc14c30dbu, 0xf8136513u,
    0xd52d2f5bu, 0xf8bfa011u, 0xa8725c73u, 0xd8ae8d5bu, 0xd252f7e5u, 0x5f82273fu,
    0xec6f4ac7u, 0x2d78196du, 0xdaa65931u, 0x7ca02911u, 0x148ecd3au, 0x13168758u,
    0xdf830e92u, 0xd1afbbecu, 0x41ca6716u, 0xd0f60c6bu, 0xbbdc1969u, 0x15c9b5a9u,
    0x0859f3e4u, 0x56f8356du, 0xd3fe5c2bu, 0xe405aca2u, 0xf1465087u, 0xddef678eu,
    0xad7ac0dbu, 0xb445bdf9u, 0x10c9d417u, 0x0b5d755bu, 0x4084ed10u, 0xd24a8c93u,
    0xcca4f729u, 0x03115690u, 0x9bb89073u, 0xaf5ea018u, 0xdb5c55e5u, 0x80a2bd72u,
    0xfac32ef8u, 0x58be6d66u, 0x33fb4c22u, 0xceb0146fu, 0x366b4dc8u, 0xc4711f29u,
    0xa032e6f3u, 0xed190e01u, 0xaeccef57u, 0x2cc30560u, 0x524b307du, 0x85da7a45u,
    0x81982889u, 0x40b56c98u, 0xcae7c989u, 0x62b1d031u, 0x5f1d8128u, 0x733ec3d7u,
    0xee2ecb7au, 0x3c5c6e01u, 0xe89dc979u, 0x15e5111du, 0xf868de5fu, 0xd6f02adeu,
    0xb7c1e038u, 0x6892bffbu, 0x7fce987bu, 0x6fe90751u, 0xe95675ecu, 0x85a1febcu,
    0x2c784491u, 0x815dd689u, 0x94ab5877u, 0x8fd5b539u, 0xcf54160au, 0xc87858dcu,
    0x6fb4985fu, 0xf213bbbfu, 0xcef9a0ddu, 0xc0fcc031u, 0xbe857515u, 0x4e71255bu,
    0x413ef65cu, 0xe92f4ab8u, 0x203fc824u, 0xf665b9bfu, 0xbfcc0590u, 0x7f440671u,
    0xf533bd95u, 0x8b813b38u, 0x55e6a3b9u, 0x784807fbu, 0x90962a28u, 0xbaad2c75u,
    0xee2d6d36u, 0x028d99fbu,
};

static const BigintBinomialCase BIGINT_BINOMIALS[] = {
    {0u, 0u, {bigint_c_0_0_limbs, 1u}, 1u},
    {6u, 1u, {bigint_c_6_1_limbs, 1u}, 3u},
    {6u, 2u, {bigint_c_6_2_limbs, 1u}, 4u},
    {14u, 4u, {bigint_c_14_4_limbs, 1u}, 10u},
    {20u, 7u, {bigint_c_20_7_limbs, 1u}, 17u},
    {6374u, 2u, {bigint_c_6374_2_limbs, 1u}, 25u},
    {6375u, 3u, {bigint_c_6375_3_limbs, 2u}, 36u},
    {6400u, 0u, {bigint_c_6400_0_limbs, 1u}, 1u},
    {6400u, 1u, {bigint_c_6400_1_limbs, 1u}, 13u},
    {6400u, 2u, {bigint_c_6400_2_limbs, 1u}, 25u},
    {6400u, 6400u, {bigint_c_6400_6400_limbs, 1u}, 1u},
    {6400u, 6401u, {NULL, 0u}, 0u},
    {6396u, 1999u, {bigint_c_6396_1999_limbs, 179u}, 5725u},
    {6400u, 3200u, {bigint_c_6400_3200_limbs, 200u}, 6394u},
};
#define BIGINT_BINOMIAL_COUNT (sizeof(BIGINT_BINOMIALS) / sizeof(BIGINT_BINOMIALS[0]))

/* 10^400 and the values derived from it (decimal-free). */
static const uint32_t bigint_zero_over_huge_exact_den_limbs[42] = {
    0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
    0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
    0xa9c10000u, 0x24377e2fu, 0x32e9f0b8u, 0x4633ebdbu, 0xa48ad3a2u, 0x96aa4193u,
    0x59e08e49u, 0xa271e1d0u, 0xe1e0c75cu, 0xb47c4813u, 0xa0e776cdu, 0xce5959bau,
    0x27fe4236u, 0xbf8dc5a4u, 0xe0af8634u, 0xeccf6cfdu, 0x9946a0b2u, 0x629853ceu,
    0x0e6ad18eu, 0x50120e5cu, 0xe5d0b7bfu, 0xadb38030u, 0x8bc150afu, 0x8ff5190bu,
    0x4582de25u, 0xc38db6e5u, 0x26fbc177u, 0xf3cb1ccfu, 0x7f91973fu, 0x0001b4ecu,
};
static const uint32_t bigint_one_over_huge_exact_num_limbs[1] = {
    0x00000001u,
};
static const uint32_t bigint_huge_minus_one_over_huge_exact_num_limbs[42] = {
    0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu,
    0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu,
    0xa9c0ffffu, 0x24377e2fu, 0x32e9f0b8u, 0x4633ebdbu, 0xa48ad3a2u, 0x96aa4193u,
    0x59e08e49u, 0xa271e1d0u, 0xe1e0c75cu, 0xb47c4813u, 0xa0e776cdu, 0xce5959bau,
    0x27fe4236u, 0xbf8dc5a4u, 0xe0af8634u, 0xeccf6cfdu, 0x9946a0b2u, 0x629853ceu,
    0x0e6ad18eu, 0x50120e5cu, 0xe5d0b7bfu, 0xadb38030u, 0x8bc150afu, 0x8ff5190bu,
    0x4582de25u, 0xc38db6e5u, 0x26fbc177u, 0xf3cb1ccfu, 0x7f91973fu, 0x0001b4ecu,
};
static const uint32_t bigint_third_of_huge_exact_num_limbs[42] = {
    0x55555555u, 0x55555555u, 0x55555555u, 0x55555555u, 0x55555555u, 0x55555555u,
    0x55555555u, 0x55555555u, 0x55555555u, 0x55555555u, 0x55555555u, 0x55555555u,
    0xe3405555u, 0xb6bd2a0fu, 0x10f8a592u, 0x6cbbf949u, 0x8c2e468bu, 0x3238c086u,
    0x1df584c3u, 0x3625f5f0u, 0xa0a04274u, 0xe6d41806u, 0xe04d2799u, 0xef731de8u,
    0x0d54c0bcu, 0x3fd9ec8cu, 0x4ae52cbcu, 0x4eefceffu, 0x33178ae6u, 0x7632c69au,
    0x5a239b2fu, 0x700604c9u, 0x4c9ae7eau, 0x39e68010u, 0x83eb1ae5u, 0x2ffc5daeu,
    0x6c80f4b7u, 0x9684924cu, 0x0cfe95d2u, 0x5143b445u, 0x2a85dd15u, 0x000091a4u,
};
static const uint32_t bigint_corner_pool_ratio_num_limbs[180] = {
    0x9d673e80u, 0xa7e5c999u, 0x4a024313u, 0x7981db3eu, 0x2031cdedu, 0x05b7b24bu,
    0xac4ae90eu, 0x1746e104u, 0x8868a34bu, 0x46154f0bu, 0xaf79dafau, 0xfb4d3526u,
    0x5dcb7e28u, 0x81276a01u, 0xe00a919cu, 0xad442cc1u, 0x6d956ec6u, 0xf8578266u,
    0xa9e5a03au, 0x74c375d0u, 0x088af591u, 0x43d923b8u, 0x5c4603d6u, 0x2be45de2u,
    0x7acb3407u, 0x511f69feu, 0xa7cb4574u, 0x05ed9e3fu, 0xba1610e5u, 0x1a6d2927u,
    0x87b5da8eu, 0x726f652au, 0xd068330fu, 0x4293b53du, 0xe352b363u, 0x4e55fd15u,
    0xd77c7c49u, 0x82428899u, 0x05b01bdeu, 0xa9905954u, 0xd8bbe045u, 0xda6368a8u,
    0xc01a9c54u, 0x63ad2b71u, 0x714bd65au, 0x0bf39a91u, 0x1f076595u, 0x750efff7u,
    0x9e489495u, 0x1bfbc32cu, 0x2f8df516u, 0xbed329cau, 0xa005b8aeu, 0xd1c8ecbcu,
    0xc73e63aeu, 0xd766e89fu, 0xc613bff9u, 0x10e44854u, 0xe89afc2fu, 0x84d8be83u,
    0x10a148a0u, 0x73dc473cu, 0x930ce1e2u, 0x2ec31eafu, 0x39efd301u, 0xc512f57cu,
    0x9c4b6642u, 0xd0939e80u, 0x28fa01f2u, 0x75d5f30bu, 0x2994026fu, 0x555b878du,
    0x5bca3bacu, 0xceccd757u, 0xba6067aau, 0xe9b39049u, 0xd688f194u, 0x6665c274u,
    0x066228dau, 0x462bed8du, 0x12cf81e6u, 0xe46fef74u, 0x72ad1074u, 0xd7545225u,
    0xb80cef60u, 0x9da47bd0u, 0x6b45d733u, 0xad6a90a9u, 0x2e81fb2bu, 0xf0a44b21u,
    0x4f4f02b8u, 0xebfd4331u, 0xfb399598u, 0x77ed0cbcu, 0xd90bf82au, 0x7ac671e0u,
    0x6325d43fu, 0xf58735e9u, 0x7717a5f7u, 0xf072bf1eu, 0xa0deb285u, 0x8e408481u,
    0x0d4d1404u, 0xd30ec15au, 0x27bd180bu, 0xcb2f1153u, 0x56a3a2cau, 0xfe655403u,
    0x40711c38u, 0x2ca9c3a9u, 0x189de486u, 0x00c7f162u, 0xe33c25d2u, 0x1f686002u,
    0x8f954640u, 0x27b3e1d0u, 0x20a39463u, 0x3b0ac0c8u, 0x65679d80u, 0x1f17a409u,
    0xc2842816u, 0x26ddeb6eu, 0xfd962017u, 0x5fa90311u, 0x598dabb6u, 0x5be442c6u,
    0x6029f64bu, 0x44fe4970u, 0x5c942797u, 0x2fe8f310u, 0x662e7b8au, 0xf255631cu,
    0xe0b28c0du, 0x30316923u, 0x4f5fe506u, 0x577b1121u, 0x1cd72ee9u, 0xc9ed52b7u,
    0x5cda0247u, 0x52bd7529u, 0x01803dd2u, 0x95d75c63u, 0x19e057bau, 0x06ebf925u,
    0xfa5877b4u, 0x6d732a57u, 0xad4209b5u, 0x7e18d88bu, 0xe10268b2u, 0x3c26477eu,
    0x3b0c0297u, 0x20d787b2u, 0x27d536a2u, 0xd92adb88u, 0x8356a41fu, 0xe2dcb80eu,
    0x39e699c6u, 0x4a2a956du, 0xdc3b1e2au, 0xa709feb4u, 0x8c3a4046u, 0x48c0e6a9u,
    0x0bc60248u, 0x050324ebu, 0x895d5dfeu, 0x60c7094au, 0x8af2c9c6u, 0xedb8ee4bu,
    0xd0678904u, 0x42b430cfu, 0x525390bau, 0x2e234db2u, 0x201f3b2bu, 0xf2e679e2u,
    0xb94d4093u, 0x5b9d9d47u, 0x1661951eu, 0x3f7039c5u, 0xb1dfe5fdu, 0x0000028fu,
};
static const uint32_t bigint_corner_pool_ratio_den_limbs[180] = {
    0x80459a00u, 0xd8679dc1u, 0x54c961f3u, 0x806b9f65u, 0x6d48e933u, 0x222c81edu,
    0x610638f4u, 0x1368bd45u, 0x05a55edeu, 0xd766e288u, 0x844f7613u, 0x5416aa51u,
    0x1d1f64cau, 0xc3a902c0u, 0x08a5fa5du, 0x66511a8au, 0x44a086aeu, 0x1702395eu,
    0xbc9bdc8fu, 0x2f4a142cu, 0xf1335450u, 0x60d5c046u, 0x7a9ba23du, 0x2b7e1784u,
    0x40eb3dd7u, 0x3b6568a7u, 0xcb651ec5u, 0x4a4aa0d0u, 0x1a281f84u, 0x6ce2d76fu,
    0xe0f654d6u, 0x1bcd2d26u, 0xa7c0e90au, 0xb37788abu, 0x7c8b9e66u, 0xb01c38c1u,
    0x5a21523eu, 0x6496e5dcu, 0x71183343u, 0x778a7f45u, 0x9ff26bbdu, 0xadb1537au,
    0xe6977b0cu, 0x7adbd39du, 0x745d11fdu, 0xacd5e762u, 0xe272979fu, 0x6fe19e4du,
    0x98dbef21u, 0xf5c62197u, 0x0d34b9dcu, 0x73b947dbu, 0x00e767a0u, 0xa6249fa9u,
    0x6bf49250u, 0x38b12065u, 0x75ab67e9u, 0x17f3a432u, 0xd9c5e2a1u, 0xa7642c8du,
    0xb6ae9fe4u, 0xa4655d53u, 0x0ed9b0cdu, 0x7d8b53b6u, 0xab080cc8u, 0xd1197180u,
    0x2c0b3ebdu, 0x90385ec7u, 0x58b83143u, 0x98c97eb1u, 0x69a0b5f5u, 0x9f8464beu,
    0x48c7dd89u, 0x71f2a8cfu, 0x72cf4963u, 0x0890bfb4u, 0xaab64b04u, 0xedfc7725u,
    0xcc32426cu, 0xf99f1effu, 0x18f7fd1cu, 0xb1ed624du, 0xae4f2192u, 0x0975c7f4u,
    0x4200a9a6u, 0xd06fddddu, 0x711958d5u, 0x92b8fbcfu, 0x4296cb89u, 0x77e782a2u,
    0x198bb8ccu, 0x554be42eu, 0xc8a33e92u, 0x18e134aau, 0xac587f96u, 0x4b7d1f03u,
    0x287c9faeu, 0xd4e3b7a3u, 0xc9e2e8a4u, 0x588cb9d1u, 0xb2ef7950u, 0xf9fdafa6u,
    0x642fdd2eu, 0x3152af4au, 0x8b91ea54u, 0x2d42b73du, 0xda0b91c5u, 0x1b02e59eu,
    0x90f9360fu, 0xde8f5ca6u, 0xdf178b66u, 0x87d04a1fu, 0x411911a9u, 0x259cd887u,
    0x4a937825u, 0x1d72a5f3u, 0x3f944a32u, 0xaed2e0b1u, 0xf8833c32u, 0xcce13843u,
    0xf88e21bfu, 0x5195c621u, 0x22278b6eu, 0x10a64fbfu, 0xc5c96344u, 0xf88dc2d3u,
    0x70c5c819u, 0x6b49472eu, 0x8af3c6c6u, 0x781fc5b4u, 0x9408e135u, 0x3516d6ebu,
    0xc36a88ecu, 0x7c1a71d4u, 0x1cd491bdu, 0xe6ba0b86u, 0x9effe664u, 0xe68497f5u,
    0x05ff979fu, 0xcd0ea933u, 0x1e498356u, 0xb944dac3u, 0x26197e1fu, 0xe704f2e7u,
    0xa8c0e75fu, 0xa45ff683u, 0xe8673579u, 0xd7b7de7au, 0x73fd766eu, 0x374c3f4cu,
    0xd72dfbbdu, 0x85a3a6a5u, 0xa7a94d17u, 0x8c54e07au, 0x38042f32u, 0x2a84bddcu,
    0x380af040u, 0xa2a42b33u, 0x85bdcb31u, 0xcbcbf846u, 0x5725b2a2u, 0x3477ce2du,
    0x68e76da0u, 0x3d5f3909u, 0x8a0ca47au, 0xee0fb9d6u, 0x0e8a4c39u, 0x0e83f46eu,
    0x87db78aeu, 0x16bd042au, 0x9ad035c7u, 0x716e02f8u, 0xa64c1b71u, 0x382ee8f8u,
    0x5d91e0aeu, 0x1ae36db1u, 0xf89866a9u, 0x1001029au, 0xf605a140u, 0x00000831u,
};
static const uint32_t bigint_corner_frontier_ratio_num_limbs[179] = {
    0xdc6ca880u, 0x45b1e066u, 0x2a04709fu, 0xd72af73eu, 0x92b95c11u, 0x4eb7b796u,
    0xa7f69f02u, 0xec48327eu, 0xb3869cdcu, 0x1906e056u, 0x9d815d70u, 0xac082a91u,
    0xc6423e37u, 0x4558cc31u, 0xfc7399d4u, 0x91b91b48u, 0xf5845b95u, 0x56de6b4bu,
    0x3e9be468u, 0x87fb14aeu, 0x7c3939a0u, 0xc7731f82u, 0x9d472c2eu, 0x931148b6u,
    0x48aceb9au, 0xe8bfa1b8u, 0xe04f8fc8u, 0xe8f30ffdu, 0x57e20964u, 0x75ddb271u,
    0x81e12ab9u, 0x0cff0968u, 0xe54fe2fbu, 0x8dfdce0au, 0x3b3417d9u, 0xf466e477u,
    0xa46d6e1cu, 0x43c48106u, 0xc320ba77u, 0x9af16cbdu, 0xb3aad378u, 0xdacffe18u,
    0x35645d2bu, 0xe70b4c6bu, 0x913e564eu, 0xdd410303u, 0xb5af72eeu, 0x02cb5353u,
    0xa8afcd24u, 0xbee7cb91u, 0xa491f956u, 0x238c59c7u, 0x6a51e066u, 0xd27ca3cau,
    0xc757f29cu, 0xf23f68aau, 0x01bd94abu, 0xa1e0315cu, 0x1b66dc7bu, 0xc0b740cau,
    0xdf639e36u, 0x95fb7817u, 0xe88bce2du, 0xb4b4c608u, 0x05fc6958u, 0xefa402b0u,
    0x7e0a8389u, 0x9a02abd8u, 0x51ab4367u, 0x481ea49au, 0x3617b76eu, 0xc4ad8ac2u,
    0x1c0fb341u, 0xf996ae1eu, 0xc8aa2ba4u, 0x34087081u, 0x16eaa2dau, 0x255e7c4cu,
    0x1e189acbu, 0xf58d9a7cu, 0xa402ae66u, 0x111d00fau, 0x5045c7f4u, 0x9b2a04e1u,
    0x6f3ba326u, 0x10acb34fu, 0xca928f41u, 0x94c102a0u, 0xa243cf6bu, 0x643a9868u,
    0x89c476a1u, 0x41c7652eu, 0xd0b36d64u, 0x1588e5b4u, 0xeeeb15bau, 0x59947594u,
    0xddfe8321u, 0xeeaabb5du, 0x8aaba705u, 0x1bb3b166u, 0x0befd0d1u, 0x2ad6fcd2u,
    0xbd52d3ddu, 0x5a3138c3u, 0x3ad4a917u, 0x3c0da015u, 0xee0006fcu, 0x5d45e5acu,
    0xeed9a470u, 0xb4269e46u, 0x83de0bf2u, 0x5dc922f5u, 0x29306b11u, 0xaf89d076u,
    0xb5b440f0u, 0x535567f0u, 0xfbb1a6efu, 0xdb3678c0u, 0x34fe2395u, 0x3ec1adf0u,
    0x8674fcb8u, 0x3c7ed205u, 0xae8d0606u, 0xcdf162ddu, 0x8fa8fb96u, 0xfd9ff285u,
    0xd13adfaeu, 0xaf49d997u, 0xec354ca1u, 0xa3295c5fu, 0x5fe4f18cu, 0x8252ffeeu,
    0xda2c5705u, 0xf736dcc1u, 0xddd1f4a3u, 0x69d69209u, 0xcedef3e2u, 0x17da844du,
    0x394d83ddu, 0x438bd5b7u, 0x600c5bb4u, 0xe542755du, 0x27f5d5c0u, 0x13a33c51u,
    0x72259488u, 0x8d7191beu, 0x5b826154u, 0x78e9d95cu, 0x510444eau, 0x79289a49u,
    0x35122fe5u, 0x097084d3u, 0x6f4068b9u, 0xbc6b1c82u, 0x4271c72au, 0xc76efba2u,
    0xa0843e43u, 0x5f1f0a91u, 0x1895138eu, 0x09cdac29u, 0x81534b45u, 0x4b249da4u,
    0x79524008u, 0xc271d975u, 0xa84c8e1fu, 0xb32d7ef4u, 0xdddf6d45u, 0x6194ed36u,
    0x5d43ef13u, 0x64cc2a03u, 0xd7edffe5u, 0x4e413bb0u, 0x903498cau, 0x571fc170u,
    0x1014e327u, 0x599e0eabu, 0xe0ed46b7u, 0x4dea8bf0u, 0x1bfd84ccu,
};
static const uint32_t bigint_corner_frontier_ratio_den_limbs[179] = {
    0x9545f980u, 0xd115a134u, 0x7e0d51ddu, 0x8580e5bau, 0xb82c1435u, 0xec2726c3u,
    0xf7e3dd06u, 0xc4d8977bu, 0x1a93d696u, 0x4b14a104u, 0xd8841850u, 0x04187fb4u,
    0x52c6baa7u, 0xd00a6495u, 0xf55acd7cu, 0xb52b51dau, 0xe08d12c0u, 0x049b41e3u,
    0xbbd3ad39u, 0x97f13e0au, 0x74abace1u, 0x56595e87u, 0xd7d5848cu, 0xb933da23u,
    0xda06c2cfu, 0xba3ee528u, 0xa0eeaf5au, 0xbad92ff9u, 0x07a61c2eu, 0x61991754u,
    0x85a3802cu, 0x26fd1c39u, 0xafefa8f1u, 0xa9f96a20u, 0xb19c478cu, 0xdd34ad65u,
    0xed484a56u, 0xcb4d8313u, 0x49622f65u, 0xd0d44639u, 0x1b007a69u, 0x906ffa4au,
    0xa02d1783u, 0xb521e541u, 0xb3bb02ecu, 0x97c3090au, 0x210e58ccu, 0x0861f9fbu,
    0xfa0f676cu, 0x3cb762b4u, 0xedb5ec04u, 0x6aa50d56u, 0x3ef5a132u, 0x7775eb5fu,
    0x5607d7d6u, 0xd6be3a00u, 0x0538be03u, 0xe5a09414u, 0x52349572u, 0x4225c25eu,
    0x9e2adaa4u, 0xc1f26847u, 0xb9a36a88u, 0x1e1e521au, 0x11f53c0au, 0xceec0810u,
    0x7a1f8a9du, 0xce080389u, 0xf501ca36u, 0xd85bedceu, 0xa247264au, 0x4e08a046u,
    0x542f19c5u, 0xecc40a5au, 0x59fe82eeu, 0x9c195185u, 0x44bfe88eu, 0x701b74e4u,
    0x5a49d061u, 0xe0a8cf74u, 0xec080b34u, 0x335702efu, 0xf0d157dcu, 0xd17e0ea3u,
    0x4db2e973u, 0x320619eeu, 0x5fb7adc3u, 0xbe4307e2u, 0xe6cb6e42u, 0x2cafc939u,
    0x9d4d63e4u, 0xc5562f8bu, 0x721a482cu, 0x409ab11eu, 0xccc1412eu, 0x0cbd60beu,
    0x99fb8964u, 0xcc003219u, 0xa002f511u, 0x531b1433u, 0x23cf7273u, 0x8084f676u,
    0x37f87b97u, 0x0e93aa4bu, 0xb07dfb46u, 0xb428e03fu, 0xca0014f4u, 0x17d1b106u,
    0xcc8ced51u, 0x1c73dad4u, 0x8b9a23d8u, 0x195b68e0u, 0x7b914134u, 0x0e9d7162u,
    0x211cc2d2u, 0xfa0037d2u, 0xf314f4cdu, 0x91a36a42u, 0x9efa6ac1u, 0xbc4509d0u,
    0x935ef628u, 0xb57c7610u, 0x0ba71212u, 0x69d42899u, 0xaefaf2c4u, 0xf8dfd790u,
    0x73b09f0cu, 0x0ddd8cc7u, 0xc49fe5e5u, 0xe97c151fu, 0x1faed4a5u, 0x86f8ffcbu,
    0x8e850510u, 0xe5a49645u, 0x9975ddebu, 0x3d83b61du, 0x6c9cdba7u, 0x478f8ce9u,
    0xabe88b97u, 0xcaa38125u, 0x2025131cu, 0xafc76018u, 0x77e18142u, 0x3ae9b4f3u,
    0x5670bd98u, 0xa854b53bu, 0x128723fdu, 0x6abd8c15u, 0xf30ccebfu, 0x6b79cedbu,
    0x9f368fb0u, 0x1c518e79u, 0x4dc13a2bu, 0x35415587u, 0xc7555580u, 0x564cf2e6u,
    0xe18cbacbu, 0x1d5d1fb4u, 0x49bf3aabu, 0x1d69047bu, 0x83f9e1cfu, 0xe16dd8edu,
    0x6bf6c018u, 0x47558c60u, 0xf8e5aa5fu, 0x19887cddu, 0x999e47d1u, 0x24bec7a4u,
    0x17cbcd3au, 0x2e647e0au, 0x87c9ffb0u, 0xeac3b312u, 0xb09dca5eu, 0x055f4451u,
    0x303ea976u, 0x0cda2c01u, 0xa2c7d426u, 0xe9bfa3d2u, 0x53f88e64u,
};
static const uint32_t bigint_prior_half_ratio_num_limbs[201] = {
    0x973f1400u, 0x25f84f66u, 0x87c57e34u, 0x5590a520u, 0x05ab3e8du, 0x37c9b2a7u,
    0x52144ae4u, 0x07d88724u, 0xe98181bfu, 0xf1596ee3u, 0xce85bc5eu, 0xcd9a0046u,
    0xab0c1aa4u, 0xdf90fc64u, 0x54b66cbbu, 0x85f26ef1u, 0x649ccec5u, 0xa3cb331du,
    0x048ddc37u, 0xc75c2efbu, 0xc9072720u, 0x101d63dcu, 0x9de11bacu, 0xf2c1ec66u,
    0x052f53f1u, 0x0c42e020u, 0x9f7a8f79u, 0x6ae4628bu, 0x1bee461eu, 0xe13c4de5u,
    0xba38d950u, 0x34d7b0d0u, 0x8aa15029u, 0xd6f394c5u, 0xc712d4d1u, 0x1892b242u,
    0x45272e2cu, 0x3321ac07u, 0x93a72641u, 0x45e18591u, 0x02e9d1dcu, 0x0fef975cu,
    0x4d93ce9eu, 0x9fa8b429u, 0x8f0a7c5bu, 0x68869a9cu, 0xd80648e0u, 0x604e309fu,
    0x091346b6u, 0xf6d8b125u, 0x869982e2u, 0xf46973ffu, 0xdfa9ef23u, 0x5caf8a31u,
    0x2329a1dbu, 0xd0ee78b7u, 0x7e70d006u, 0xeab7edd8u, 0x995e6ce4u, 0x13bfaf4fu,
    0x5b9c42d3u, 0x2f1b01cdu, 0xa9801f92u, 0x3d46eb65u, 0x3e1d369du, 0x30a0dc95u,
    0x8185a7e5u, 0xa745e9b8u, 0xf20d262fu, 0x89313bf9u, 0x8dd15c68u, 0xd593dbd7u,
    0x29c2ff1au, 0xda577026u, 0x9bb64598u, 0x45cc466au, 0x9201d1e0u, 0x5cab399fu,
    0xa453659fu, 0x5d342d95u, 0xade0d970u, 0x4f39e9a6u, 0xe888a56au, 0xcf367111u,
    0xdb55466au, 0x86cd86ddu, 0x830b2c7fu, 0x79ed540fu, 0x61d69fb2u, 0x97321f39u,
    0x34e4af5eu, 0x49eb445fu, 0x22e7bce2u, 0xd1c419e1u, 0x25363139u, 0x520b1338u,
    0xf78d4e8cu, 0x1b5ea39eu, 0x9f6c0c3fu, 0xa159be8au, 0x80215395u, 0xeec4e717u,
    0x2146f12fu, 0xbc78de97u, 0x390eeb77u, 0xa949bfc6u, 0x3862b64cu, 0xf26f76f0u,
    0xb4cffd9cu, 0x5b50dee8u, 0x9583a9a5u, 0x85e6f9b9u, 0x0d1ab914u, 0xdaea9dc5u,
    0x6f26bc29u, 0x5d3dde0bu, 0x1f5ae6b8u, 0xd2015f2du, 0xf9055b15u, 0x999bcd00u,
    0xe63621eeu, 0x14ad10e9u, 0x62089d3du, 0x039b3cb6u, 0x3f3daab4u, 0x595ec9acu,
    0x6468a310u, 0x1e9bd2e8u, 0xeb801dbfu, 0x46edf359u, 0xeeeea2a2u, 0x308e7ac7u,
    0x7e6abc56u, 0x67c6b0f8u, 0xdadb284du, 0x103af251u, 0x7d93488eu, 0xa3dd30a6u,
    0x0e118ac4u, 0x58ba11feu, 0x830d9da6u, 0x1ed1339au, 0x0231b710u, 0xf2401bb6u,
    0x87cb2247u, 0x4c57873eu, 0xc537ad55u, 0x98ff6e09u, 0x3d4c4e17u, 0x86058328u,
    0x7c46e717u, 0xb92f1452u, 0x01afcb13u, 0x85c33889u, 0xabde1cafu, 0x2af86284u,
    0xedfab709u, 0xdbcd7253u, 0x515733a8u, 0xaeaa6e68u, 0xf0ce78d1u, 0x90900424u,
    0x48ef7aa0u, 0x835f1821u, 0xb4566b72u, 0xaf55f5dbu, 0x1edba491u, 0xba17e321u,
    0xf772c67eu, 0x2a5fca78u, 0x9672069bu, 0xe0db7abdu, 0xb8c20b76u, 0x68f03964u,
    0xdf591b06u, 0x14f9b2abu, 0x5dd1d5d1u, 0xef594fc2u, 0x9b138405u, 0xe056c81fu,
    0x5170ad49u, 0xf6aad8f4u, 0x305ad651u, 0x57626e9bu, 0x84378fecu, 0x8652facdu,
    0x930781d4u, 0xcf25ff2fu, 0x1d45cd62u, 0xf791d513u, 0x76459407u, 0xd2508dddu,
    0x06c1ccb6u, 0xcf6447f9u, 0xc2fe8f4fu, 0x8463c5b1u, 0x550ef9dfu, 0x74abbd8fu,
    0x37d52c1du, 0xea04cd21u, 0x0000001fu,
};
static const uint32_t bigint_prior_half_ratio_den_limbs[201] = {
    0x2e7e2800u, 0x4bf09ecdu, 0x0f8afc68u, 0xab214a41u, 0x0b567d1au, 0x6f93654eu,
    0xa42895c8u, 0x0fb10e48u, 0xd303037eu, 0xe2b2ddc7u, 0x9d0b78bdu, 0x9b34008du,
    0x56183549u, 0xbf21f8c9u, 0xa96cd977u, 0x0be4dde2u, 0xc9399d8bu, 0x4796663au,
    0x091bb86fu, 0x8eb85df6u, 0x920e4e41u, 0x203ac7b9u, 0x3bc23758u, 0xe583d8cdu,
    0x0a5ea7e3u, 0x1885c040u, 0x3ef51ef2u, 0xd5c8c517u, 0x37dc8c3cu, 0xc2789bcau,
    0x7471b2a1u, 0x69af61a1u, 0x1542a052u, 0xade7298bu, 0x8e25a9a3u, 0x31256485u,
    0x8a4e5c58u, 0x6643580eu, 0x274e4c82u, 0x8bc30b23u, 0x05d3a3b8u, 0x1fdf2eb8u,
    0x9b279d3cu, 0x3f516852u, 0x1e14f8b7u, 0xd10d3539u, 0xb00c91c0u, 0xc09c613fu,
    0x12268d6cu, 0xedb1624au, 0x0d3305c5u, 0xe8d2e7ffu, 0xbf53de47u, 0xb95f1463u,
    0x465343b6u, 0xa1dcf16eu, 0xfce1a00du, 0xd56fdbb0u, 0x32bcd9c9u, 0x277f5e9fu,
    0xb73885a6u, 0x5e36039au, 0x53003f24u, 0x7a8dd6cbu, 0x7c3a6d3au, 0x6141b92au,
    0x030b4fcau, 0x4e8bd371u, 0xe41a4c5fu, 0x126277f3u, 0x1ba2b8d1u, 0xab27b7afu,
    0x5385fe35u, 0xb4aee04cu, 0x376c8b31u, 0x8b988cd5u, 0x2403a3c0u, 0xb956733fu,
    0x48a6cb3eu, 0xba685b2bu, 0x5bc1b2e0u, 0x9e73d34du, 0xd1114ad4u, 0x9e6ce223u,
    0xb6aa8cd5u, 0x0d9b0dbbu, 0x061658ffu, 0xf3daa81fu, 0xc3ad3f64u, 0x2e643e72u,
    0x69c95ebdu, 0x93d688beu, 0x45cf79c4u, 0xa38833c2u, 0x4a6c6273u, 0xa4162670u,
    0xef1a9d18u, 0x36bd473du, 0x3ed8187eu, 0x42b37d15u, 0x0042a72bu, 0xdd89ce2fu,
    0x428de25fu, 0x78f1bd2eu, 0x721dd6efu, 0x52937f8cu, 0x70c56c99u, 0xe4deede0u,
    0x699ffb39u, 0xb6a1bdd1u, 0x2b07534au, 0x0bcdf373u, 0x1a357229u, 0xb5d53b8au,
    0xde4d7853u, 0xba7bbc16u, 0x3eb5cd70u, 0xa402be5au, 0xf20ab62bu, 0x33379a01u,
    0xcc6c43ddu, 0x295a21d3u, 0xc4113a7au, 0x0736796cu, 0x7e7b5568u, 0xb2bd9358u,
    0xc8d14620u, 0x3d37a5d0u, 0xd7003b7eu, 0x8ddbe6b3u, 0xdddd4544u, 0x611cf58fu,
    0xfcd578acu, 0xcf8d61f0u, 0xb5b6509au, 0x2075e4a3u, 0xfb26911cu, 0x47ba614cu,
    0x1c231589u, 0xb17423fcu, 0x061b3b4cu, 0x3da26735u, 0x04636e20u, 0xe480376cu,
    0x0f96448fu, 0x98af0e7du, 0x8a6f5aaau, 0x31fedc13u, 0x7a989c2fu, 0x0c0b0650u,
    0xf88dce2fu, 0x725e28a4u, 0x035f9627u, 0x0b867112u, 0x57bc395fu, 0x55f0c509u,
    0xdbf56e12u, 0xb79ae4a7u, 0xa2ae6751u, 0x5d54dcd0u, 0xe19cf1a3u, 0x21200849u,
    0x91def541u, 0x06be3042u, 0x68acd6e5u, 0x5eabebb7u, 0x3db74923u, 0x742fc642u,
    0xeee58cfdu, 0x54bf94f1u, 0x2ce40d36u, 0xc1b6f57bu, 0x718416edu, 0xd1e072c9u,
    0xbeb2360cu, 0x29f36557u, 0xbba3aba2u, 0xdeb29f84u, 0x3627080bu, 0xc0ad903fu,
    0xa2e15a93u, 0xed55b1e8u, 0x60b5aca3u, 0xaec4dd36u, 0x086f1fd8u, 0x0ca5f59bu,
    0x260f03a9u, 0x9e4bfe5fu, 0x3a8b9ac5u, 0xef23aa26u, 0xec8b280fu, 0xa4a11bbau,
    0x0d83996du, 0x9ec88ff2u, 0x85fd1e9fu, 0x08c78b63u, 0xaa1df3bfu, 0xe9577b1eu,
    0x6faa583au, 0xd4099a42u, 0x0000003fu,
};

static const BigintRatioCase BIGINT_RATIOS[] = {
    {"zero_over_huge_exact", "tests.test_probability.LargeBoardTests.test_exact_ratios_reserve_endpoints_for_certainty", {NULL, 0u}, {bigint_zero_over_huge_exact_den_limbs, 42u}, 1u, UINT64_C(0x0000000000000000), 0.0}, /* 0.0 */
    {"huge_over_huge_exact", "same", {bigint_zero_over_huge_exact_den_limbs, 42u}, {bigint_zero_over_huge_exact_den_limbs, 42u}, 1u, UINT64_C(0x3ff0000000000000), 0.0}, /* 1.0 */
    {"one_over_huge_exact", "same", {bigint_one_over_huge_exact_num_limbs, 1u}, {bigint_zero_over_huge_exact_den_limbs, 42u}, 1u, UINT64_C(0x0000000000000001), 0.0}, /* 5e-324 */
    {"huge_minus_one_over_huge_exact", "same", {bigint_huge_minus_one_over_huge_exact_num_limbs, 42u}, {bigint_zero_over_huge_exact_den_limbs, 42u}, 1u, UINT64_C(0x3fefffffffffffff), 0.0}, /* 0.9999999999999999 */
    {"one_over_huge_approximate", "same", {bigint_one_over_huge_exact_num_limbs, 1u}, {bigint_zero_over_huge_exact_den_limbs, 42u}, 0u, UINT64_C(0x0000000000000000), 0.0}, /* 0.0 */
    {"huge_minus_one_over_huge_approximate", "same", {bigint_huge_minus_one_over_huge_exact_num_limbs, 42u}, {bigint_zero_over_huge_exact_den_limbs, 42u}, 0u, UINT64_C(0x3ff0000000000000), 0.0}, /* 1.0 */
    {"third_of_huge_exact", "same (baseline: assertAlmostEqual places=15)", {bigint_third_of_huge_exact_num_limbs, 42u}, {bigint_zero_over_huge_exact_den_limbs, 42u}, 1u, UINT64_C(0x3fd5555555555555), 5e-16}, /* 0.3333333333333333 */
    {"corner_pool_ratio", "tests.test_probability.LargeBoardTests.test_huge_binomial_weights_keep_frontier_odds_exact (pool numerator / (U*Z))", {bigint_corner_pool_ratio_num_limbs, 180u}, {bigint_corner_pool_ratio_den_limbs, 180u}, 1u, UINT64_C(0x3fd400a3f14552c6), 0.0}, /* 0.31253908692933086 */
    {"corner_frontier_ratio", "tests.test_probability.LargeBoardTests.test_huge_binomial_weights_keep_frontier_odds_exact (frontier numerator / Z)", {bigint_corner_frontier_ratio_num_limbs, 179u}, {bigint_corner_frontier_ratio_den_limbs, 179u}, 1u, UINT64_C(0x3fd5555555555555), 0.0}, /* 0.3333333333333333 */
    {"prior_half_ratio", "tests.test_probability.LargeBoardTests.test_uniform_prior_on_largest_board_does_not_overflow (pool numerator / (U*Z))", {bigint_prior_half_ratio_num_limbs, 201u}, {bigint_prior_half_ratio_den_limbs, 201u}, 1u, UINT64_C(0x3fe0000000000000), 0.0}, /* 0.5 */
};
#define BIGINT_RATIO_COUNT (sizeof(BIGINT_RATIOS) / sizeof(BIGINT_RATIOS[0]))

/* Polynomial division: divisor H[k] = C(200,k), k=0..4; quotient G[k] = C(300,k),
 * k=0..5; dividend Q = H*G (two-limb coefficients). The truncated case keeps
 * only exponents <= 6 (as the solver truncates above the remaining mine count);
 * the inexact case adds 1 to Q[3]. */
static const uint32_t bigint_poly_exact_full_dividend_0_limbs[1] = {
    0x00000001u,
};
static const uint32_t bigint_poly_exact_full_dividend_1_limbs[1] = {
    0x000001f4u,
};
static const uint32_t bigint_poly_exact_full_dividend_2_limbs[1] = {
    0x0001e74eu,
};
static const uint32_t bigint_poly_exact_full_dividend_3_limbs[1] = {
    0x013bfc94u,
};
static const uint32_t bigint_poly_exact_full_dividend_4_limbs[1] = {
    0x995d56d5u,
};
static const uint32_t bigint_poly_exact_full_dividend_5_limbs[2] = {
    0xd6a1e5f8u, 0x0000003au,
};
static const uint32_t bigint_poly_exact_full_dividend_6_limbs[2] = {
    0x66549d50u, 0x00001182u,
};
static const uint32_t bigint_poly_exact_full_dividend_7_limbs[2] = {
    0x4298d490u, 0x0003f3aau,
};
static const uint32_t bigint_poly_exact_full_dividend_8_limbs[2] = {
    0x7d87b15au, 0x00a764f1u,
};
static const uint32_t bigint_poly_exact_full_dividend_9_limbs[2] = {
    0x2447cad0u, 0x11944676u,
};
static const FixtureBig bigint_poly_exact_full_dividend[10] = {
    {bigint_poly_exact_full_dividend_0_limbs, 1u}, {bigint_poly_exact_full_dividend_1_limbs, 1u}, {bigint_poly_exact_full_dividend_2_limbs, 1u}, {bigint_poly_exact_full_dividend_3_limbs, 1u}, {bigint_poly_exact_full_dividend_4_limbs, 1u}, {bigint_poly_exact_full_dividend_5_limbs, 2u}, {bigint_poly_exact_full_dividend_6_limbs, 2u}, {bigint_poly_exact_full_dividend_7_limbs, 2u}, {bigint_poly_exact_full_dividend_8_limbs, 2u}, {bigint_poly_exact_full_dividend_9_limbs, 2u},
};
static const uint32_t bigint_poly_exact_full_divisor_0_limbs[1] = {
    0x00000001u,
};
static const uint32_t bigint_poly_exact_full_divisor_1_limbs[1] = {
    0x000000c8u,
};
static const uint32_t bigint_poly_exact_full_divisor_2_limbs[1] = {
    0x00004dbcu,
};
static const uint32_t bigint_poly_exact_full_divisor_3_limbs[1] = {
    0x00140a78u,
};
static const uint32_t bigint_poly_exact_full_divisor_4_limbs[1] = {
    0x03db0396u,
};
static const FixtureBig bigint_poly_exact_full_divisor[5] = {
    {bigint_poly_exact_full_divisor_0_limbs, 1u}, {bigint_poly_exact_full_divisor_1_limbs, 1u}, {bigint_poly_exact_full_divisor_2_limbs, 1u}, {bigint_poly_exact_full_divisor_3_limbs, 1u}, {bigint_poly_exact_full_divisor_4_limbs, 1u},
};
static const uint32_t bigint_poly_exact_full_quotient_0_limbs[1] = {
    0x00000001u,
};
static const uint32_t bigint_poly_exact_full_quotient_1_limbs[1] = {
    0x0000012cu,
};
static const uint32_t bigint_poly_exact_full_quotient_2_limbs[1] = {
    0x0000af32u,
};
static const uint32_t bigint_poly_exact_full_quotient_3_limbs[1] = {
    0x0043fabcu,
};
static const uint32_t bigint_poly_exact_full_quotient_4_limbs[1] = {
    0x13b77907u,
};
static const uint32_t bigint_poly_exact_full_quotient_5_limbs[2] = {
    0x8f3a6338u, 0x00000004u,
};
static const FixtureBig bigint_poly_exact_full_quotient[10] = {
    {bigint_poly_exact_full_quotient_0_limbs, 1u}, {bigint_poly_exact_full_quotient_1_limbs, 1u}, {bigint_poly_exact_full_quotient_2_limbs, 1u}, {bigint_poly_exact_full_quotient_3_limbs, 1u}, {bigint_poly_exact_full_quotient_4_limbs, 1u}, {bigint_poly_exact_full_quotient_5_limbs, 2u}, {NULL, 0u}, {NULL, 0u}, {NULL, 0u}, {NULL, 0u},
};
static const uint32_t bigint_poly_exact_truncated_tmax6_dividend_0_limbs[1] = {
    0x00000001u,
};
static const uint32_t bigint_poly_exact_truncated_tmax6_dividend_1_limbs[1] = {
    0x000001f4u,
};
static const uint32_t bigint_poly_exact_truncated_tmax6_dividend_2_limbs[1] = {
    0x0001e74eu,
};
static const uint32_t bigint_poly_exact_truncated_tmax6_dividend_3_limbs[1] = {
    0x013bfc94u,
};
static const uint32_t bigint_poly_exact_truncated_tmax6_dividend_4_limbs[1] = {
    0x995d56d5u,
};
static const uint32_t bigint_poly_exact_truncated_tmax6_dividend_5_limbs[2] = {
    0xd6a1e5f8u, 0x0000003au,
};
static const uint32_t bigint_poly_exact_truncated_tmax6_dividend_6_limbs[2] = {
    0x66549d50u, 0x00001182u,
};
static const FixtureBig bigint_poly_exact_truncated_tmax6_dividend[7] = {
    {bigint_poly_exact_truncated_tmax6_dividend_0_limbs, 1u}, {bigint_poly_exact_truncated_tmax6_dividend_1_limbs, 1u}, {bigint_poly_exact_truncated_tmax6_dividend_2_limbs, 1u}, {bigint_poly_exact_truncated_tmax6_dividend_3_limbs, 1u}, {bigint_poly_exact_truncated_tmax6_dividend_4_limbs, 1u}, {bigint_poly_exact_truncated_tmax6_dividend_5_limbs, 2u}, {bigint_poly_exact_truncated_tmax6_dividend_6_limbs, 2u},
};
static const uint32_t bigint_poly_exact_truncated_tmax6_divisor_0_limbs[1] = {
    0x00000001u,
};
static const uint32_t bigint_poly_exact_truncated_tmax6_divisor_1_limbs[1] = {
    0x000000c8u,
};
static const uint32_t bigint_poly_exact_truncated_tmax6_divisor_2_limbs[1] = {
    0x00004dbcu,
};
static const uint32_t bigint_poly_exact_truncated_tmax6_divisor_3_limbs[1] = {
    0x00140a78u,
};
static const uint32_t bigint_poly_exact_truncated_tmax6_divisor_4_limbs[1] = {
    0x03db0396u,
};
static const FixtureBig bigint_poly_exact_truncated_tmax6_divisor[5] = {
    {bigint_poly_exact_truncated_tmax6_divisor_0_limbs, 1u}, {bigint_poly_exact_truncated_tmax6_divisor_1_limbs, 1u}, {bigint_poly_exact_truncated_tmax6_divisor_2_limbs, 1u}, {bigint_poly_exact_truncated_tmax6_divisor_3_limbs, 1u}, {bigint_poly_exact_truncated_tmax6_divisor_4_limbs, 1u},
};
static const uint32_t bigint_poly_exact_truncated_tmax6_quotient_0_limbs[1] = {
    0x00000001u,
};
static const uint32_t bigint_poly_exact_truncated_tmax6_quotient_1_limbs[1] = {
    0x0000012cu,
};
static const uint32_t bigint_poly_exact_truncated_tmax6_quotient_2_limbs[1] = {
    0x0000af32u,
};
static const uint32_t bigint_poly_exact_truncated_tmax6_quotient_3_limbs[1] = {
    0x0043fabcu,
};
static const uint32_t bigint_poly_exact_truncated_tmax6_quotient_4_limbs[1] = {
    0x13b77907u,
};
static const uint32_t bigint_poly_exact_truncated_tmax6_quotient_5_limbs[2] = {
    0x8f3a6338u, 0x00000004u,
};
static const FixtureBig bigint_poly_exact_truncated_tmax6_quotient[7] = {
    {bigint_poly_exact_truncated_tmax6_quotient_0_limbs, 1u}, {bigint_poly_exact_truncated_tmax6_quotient_1_limbs, 1u}, {bigint_poly_exact_truncated_tmax6_quotient_2_limbs, 1u}, {bigint_poly_exact_truncated_tmax6_quotient_3_limbs, 1u}, {bigint_poly_exact_truncated_tmax6_quotient_4_limbs, 1u}, {bigint_poly_exact_truncated_tmax6_quotient_5_limbs, 2u}, {NULL, 0u},
};
static const uint32_t bigint_poly_inexact_remainder_dividend_0_limbs[1] = {
    0x00000001u,
};
static const uint32_t bigint_poly_inexact_remainder_dividend_1_limbs[1] = {
    0x000001f4u,
};
static const uint32_t bigint_poly_inexact_remainder_dividend_2_limbs[1] = {
    0x0001e74eu,
};
static const uint32_t bigint_poly_inexact_remainder_dividend_3_limbs[1] = {
    0x013bfc95u,
};
static const uint32_t bigint_poly_inexact_remainder_dividend_4_limbs[1] = {
    0x995d56d5u,
};
static const uint32_t bigint_poly_inexact_remainder_dividend_5_limbs[2] = {
    0xd6a1e5f8u, 0x0000003au,
};
static const uint32_t bigint_poly_inexact_remainder_dividend_6_limbs[2] = {
    0x66549d50u, 0x00001182u,
};
static const uint32_t bigint_poly_inexact_remainder_dividend_7_limbs[2] = {
    0x4298d490u, 0x0003f3aau,
};
static const uint32_t bigint_poly_inexact_remainder_dividend_8_limbs[2] = {
    0x7d87b15au, 0x00a764f1u,
};
static const uint32_t bigint_poly_inexact_remainder_dividend_9_limbs[2] = {
    0x2447cad0u, 0x11944676u,
};
static const FixtureBig bigint_poly_inexact_remainder_dividend[10] = {
    {bigint_poly_inexact_remainder_dividend_0_limbs, 1u}, {bigint_poly_inexact_remainder_dividend_1_limbs, 1u}, {bigint_poly_inexact_remainder_dividend_2_limbs, 1u}, {bigint_poly_inexact_remainder_dividend_3_limbs, 1u}, {bigint_poly_inexact_remainder_dividend_4_limbs, 1u}, {bigint_poly_inexact_remainder_dividend_5_limbs, 2u}, {bigint_poly_inexact_remainder_dividend_6_limbs, 2u}, {bigint_poly_inexact_remainder_dividend_7_limbs, 2u}, {bigint_poly_inexact_remainder_dividend_8_limbs, 2u}, {bigint_poly_inexact_remainder_dividend_9_limbs, 2u},
};
static const uint32_t bigint_poly_inexact_remainder_divisor_0_limbs[1] = {
    0x00000001u,
};
static const uint32_t bigint_poly_inexact_remainder_divisor_1_limbs[1] = {
    0x000000c8u,
};
static const uint32_t bigint_poly_inexact_remainder_divisor_2_limbs[1] = {
    0x00004dbcu,
};
static const uint32_t bigint_poly_inexact_remainder_divisor_3_limbs[1] = {
    0x00140a78u,
};
static const uint32_t bigint_poly_inexact_remainder_divisor_4_limbs[1] = {
    0x03db0396u,
};
static const FixtureBig bigint_poly_inexact_remainder_divisor[5] = {
    {bigint_poly_inexact_remainder_divisor_0_limbs, 1u}, {bigint_poly_inexact_remainder_divisor_1_limbs, 1u}, {bigint_poly_inexact_remainder_divisor_2_limbs, 1u}, {bigint_poly_inexact_remainder_divisor_3_limbs, 1u}, {bigint_poly_inexact_remainder_divisor_4_limbs, 1u},
};
static const BigintPolyDivisionCase BIGINT_POLY_DIVISIONS[] = {
    {"exact_full", bigint_poly_exact_full_dividend, 10u, bigint_poly_exact_full_divisor, 5u, bigint_poly_exact_full_quotient, 10u, 1u},
    {"exact_truncated_tmax6", bigint_poly_exact_truncated_tmax6_dividend, 7u, bigint_poly_exact_truncated_tmax6_divisor, 5u, bigint_poly_exact_truncated_tmax6_quotient, 7u, 1u},
    {"inexact_remainder", bigint_poly_inexact_remainder_dividend, 10u, bigint_poly_inexact_remainder_divisor, 5u, NULL, 0u, 0u},
};
#define BIGINT_POLY_DIVISION_COUNT (sizeof(BIGINT_POLY_DIVISIONS) / sizeof(BIGINT_POLY_DIVISIONS[0]))
