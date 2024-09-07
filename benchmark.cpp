// Copyright (c) 2024 Pyarelal Knowles, MIT License

// #define ANKERL_NANOBENCH_IMPLEMENT
#include <decodeless/offset_ptr.hpp>
#include <decodeless/writer.hpp>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <nanobench.h>
#include <ostream>
#include <stdexcept>
#include <stdio.h>

#define MB_PER_RUN 256

namespace nb = ankerl::nanobench;
namespace fs = std::filesystem;

fs::path exePath() { return std::filesystem::canonical("/proc/self/exe"); }

struct TmpFile {
    TmpFile(std::string_view filename)
        : path{exePath().parent_path() / filename} {}
    ~TmpFile() { fs::remove(path); }
    TmpFile() = delete;
    TmpFile(const TmpFile& other) = delete;
    TmpFile& operator=(const TmpFile& other) = delete;
    fs::path path;
};

#define CHECK(expression) check(expression, #expression)
void check(bool result, const char* text) {
    if (!result)
        throw std::runtime_error("failed '" + std::string(text) + "'");
}

TEST(Benchmark, WriteSequentialInts) {
    constexpr int32_t numIntsToWrite = MB_PER_RUN * 1024 * 1024 / sizeof(int32_t);
    const TmpFile     resultFwrite("result_fwrite.dat");
    const TmpFile     resultOfstream("result_ofstream.dat");
    const TmpFile     resultMmap("result_mmap.dat");
    const TmpFile     resultWriter("result_writer.dat");
    bool              driveIsRotational =
        system(("test 1 = $(lsblk -o ROTA $(df --output=source " +
                resultFwrite.path.parent_path().string() + " | tail -1) | tail -1)")
                   .c_str()) == 0;
    printf("Drive: %s\n", driveIsRotational ? "rotational" : "not rotational");
    printf("Writing %zu bytes\n", numIntsToWrite * sizeof(int32_t));
    nb::Bench()
        //.minEpochTime(std::chrono::milliseconds(50))
        //.minEpochIterations(10)
        //.warmup(1)
        .epochs(1)
        .relative(true)
        .run("fwrite",
             [&] {
                 FILE* f = fopen(resultFwrite.path.string().c_str(), "wb");
                 for (int32_t i = 0; i < numIntsToWrite; ++i)
                     fwrite(&i, sizeof(i), 1, f);
                 fflush(f);
                 fclose(f);
                 sync();
             })
        .run("ofstream",
             [&] {
                 std::ofstream f(resultOfstream.path, std::ios::binary);
                 for (int32_t i = 0; i < numIntsToWrite; ++i)
                     f.write(reinterpret_cast<const char*>(&i), sizeof(i));
                 f.flush();
                 sync();
             })
        .run("mmap",
             [&] {
                 int    f = open(resultMmap.path.c_str(), O_RDWR | O_CREAT | O_TRUNC, (mode_t)0600);
                 size_t elements = numIntsToWrite;
                 size_t size = sizeof(int32_t) * elements;
                 CHECK(ftruncate(f, size) == 0);
                 auto mapped = (int32_t*)mmap(0, size, PROT_WRITE, MAP_SHARED, f, 0);
                 CHECK(mapped != nullptr);
                 for (size_t i = 0; i < elements; ++i)
                     mapped[i] = i;
                 CHECK(fsync(f) == 0);
                 CHECK(munmap(mapped, size) == 0);
                 sync();
             })
        .run("writer", [&] {
            decodeless::file_writer f(resultWriter.path, 1024 * 1024 * 1024, 4);
            for (int32_t i = 0; i < numIntsToWrite; ++i)
                f.create<int32_t>(i);
            sync();
        });
    decodeless::file resultFwriteFile(resultFwrite.path);
    decodeless::file resultOfstreamFile(resultOfstream.path);
    decodeless::file resultMmapFile(resultMmap.path);
    decodeless::file resultWriterFile(resultWriter.path);
    ASSERT_EQ(resultFwriteFile.size(), resultOfstreamFile.size());
    ASSERT_EQ(resultFwriteFile.size(), resultMmapFile.size());
    ASSERT_EQ(resultFwriteFile.size(), resultWriterFile.size());
    EXPECT_EQ(memcmp(resultFwriteFile.data(), resultOfstreamFile.data(), resultOfstreamFile.size()),
              0);
    EXPECT_EQ(memcmp(resultFwriteFile.data(), resultMmapFile.data(), resultMmapFile.size()), 0);
    EXPECT_EQ(memcmp(resultFwriteFile.data(), resultWriterFile.data(), resultWriterFile.size()), 0);
}

TEST(Benchmark, WriteSequentialBlocks) {
    constexpr int32_t numIntsPerBlock = 10000;
    constexpr int32_t numIntsToWrite = MB_PER_RUN * 1024 * 1024 / sizeof(int32_t);
    constexpr int32_t numBlocksToWrite = numIntsToWrite / numIntsPerBlock;
    static_assert(numBlocksToWrite > 10);
    const TmpFile resultFwrite("result_fwrite.dat");
    const TmpFile resultOfstream("result_ofstream.dat");
    const TmpFile resultMmap("result_mmap.dat");
    const TmpFile resultWriterCopy("result_writer_copy.dat");
    const TmpFile resultWriterFill("result_writer_fill.dat");
    printf("Writing %zu bytes in %i blocks of %zu bytes\n",
           numBlocksToWrite * numIntsPerBlock * sizeof(int32_t), numBlocksToWrite,
           numIntsPerBlock * sizeof(int32_t));
    nb::Bench()
        //.minEpochTime(std::chrono::milliseconds(50))
        //.minEpochIterations(10)
        //.warmup(1)
        .epochs(1)
        .relative(true)
        .run("fwrite",
             [&] {
                 FILE* f = fopen(resultFwrite.path.string().c_str(), "wb");
                 for (int32_t i = 0; i < numBlocksToWrite; ++i) {
                     std::vector<int32_t> bulk(numIntsPerBlock, i);
                     fwrite(bulk.data(), sizeof(*bulk.data()), bulk.size(), f);
                 }
                 fflush(f);
                 fclose(f);
                 sync();
             })
        .run("ofstream",
             [&] {
                 std::ofstream f(resultOfstream.path, std::ios::binary);
                 for (int32_t i = 0; i < numBlocksToWrite; ++i) {
                     std::vector<int32_t> bulk(numIntsPerBlock, i);
                     f.write(reinterpret_cast<const char*>(bulk.data()),
                             sizeof(*bulk.data()) * bulk.size());
                 }
                 f.flush();
                 sync();
             })
        .run("mmap",
             [&] {
                 int    f = open(resultMmap.path.c_str(), O_RDWR | O_CREAT | O_TRUNC, (mode_t)0600);
                 size_t elements = numBlocksToWrite * numIntsPerBlock;
                 size_t size = sizeof(int32_t) * elements;
                 CHECK(ftruncate(f, size) == 0);
                 auto mapped = (int32_t*)mmap(0, size, PROT_WRITE, MAP_SHARED, f, 0);
                 CHECK(mapped != nullptr);
                 for (int32_t i = 0; i < (int32_t)(elements / numIntsPerBlock); ++i)
                     std::ranges::fill(
                         std::span(mapped, elements).subspan(i * numIntsPerBlock, numIntsPerBlock),
                         i);
                 CHECK(fsync(f) == 0);
                 CHECK(munmap(mapped, size) == 0);
                 sync();
             })
        .run("writer::createArray(copy)",
             [&] {
                 decodeless::file_writer f(resultWriterCopy.path, 1024 * 1024 * 1024, 4);
                 for (int32_t i = 0; i < numBlocksToWrite; ++i) {
                     std::vector<int32_t> bulk(numIntsPerBlock, i);
                     f.createArray<int32_t>(bulk);
                 }
                 sync();
             })
        .run("std::ranges::fill(writer::createArray())", [&] {
            decodeless::file_writer f(resultWriterFill.path, 1024 * 1024 * 1024, 4);
            for (int32_t i = 0; i < numBlocksToWrite; ++i)
                std::ranges::fill(f.createArray<int32_t>(numIntsPerBlock), i);
            sync();
        });
    decodeless::file resultFwriteFile(resultFwrite.path);
    decodeless::file resultOfstreamFile(resultOfstream.path);
    decodeless::file resultMmapFile(resultMmap.path);
    decodeless::file resultWriterCopyFile(resultWriterCopy.path);
    decodeless::file resultWriterFillFile(resultWriterFill.path);
    ASSERT_EQ(resultFwriteFile.size(), resultOfstreamFile.size());
    ASSERT_EQ(resultFwriteFile.size(), resultMmapFile.size());
    ASSERT_EQ(resultFwriteFile.size(), resultWriterCopyFile.size());
    ASSERT_EQ(resultFwriteFile.size(), resultWriterFillFile.size());
    EXPECT_EQ(memcmp(resultFwriteFile.data(), resultOfstreamFile.data(), resultOfstreamFile.size()),
              0);
    EXPECT_EQ(memcmp(resultFwriteFile.data(), resultMmapFile.data(), resultMmapFile.size()), 0);
    EXPECT_EQ(
        memcmp(resultFwriteFile.data(), resultWriterCopyFile.data(), resultWriterCopyFile.size()),
        0);
    EXPECT_EQ(
        memcmp(resultFwriteFile.data(), resultWriterFillFile.data(), resultWriterFillFile.size()),
        0);
}
