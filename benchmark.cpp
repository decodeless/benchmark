// Copyright (c) 2024 Pyarelal Knowles, MIT License

#define _CRT_SECURE_NO_DEPRECATE

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

#ifdef _WIN32
    #include <memoryapi.h>
    #include <sysinfoapi.h>
    #include <windows.h>
#endif

#define MB_PER_RUN 256

namespace nb = ankerl::nanobench;
namespace fs = std::filesystem;

fs::path exePath() {
#ifdef _WIN32
    wchar_t path[MAX_PATH] = {0};
    GetModuleFileNameW(NULL, path, MAX_PATH);
    return path;
#else
    return std::filesystem::canonical("/proc/self/exe");
#endif
}

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

TEST(WriteFile, SequentialInts) {
    constexpr int32_t numIntsToWrite = MB_PER_RUN * 1024 * 1024 / sizeof(int32_t);
    const TmpFile     resultFwrite("result_fwrite.dat");
    const TmpFile     resultOfstream("result_ofstream.dat");
    const TmpFile     resultMmap("result_mmap.dat");
    const TmpFile     resultWriter("result_writer.dat");

#ifndef _WIN32
    bool driveIsRotational =
        system(("test 1 = $(lsblk -o ROTA $(df --output=source " +
                resultFwrite.path.parent_path().string() + " | tail -1) | tail -1)")
                   .c_str()) == 0;
    printf("Drive: %s\n", driveIsRotational ? "rotational" : "not rotational");
#endif

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
#ifdef _WIN32
                 FlushFileBuffers((HANDLE)_get_osfhandle(_fileno(f)));
                 fclose(f);
#else
                 fclose(f);
                 sync();
#endif
             })
        .run("ofstream",
             [&] {
                 std::ofstream f(resultOfstream.path, std::ios::binary);
                 for (int32_t i = 0; i < numIntsToWrite; ++i)
                     f.write(reinterpret_cast<const char*>(&i), sizeof(i));
                 f.flush();
#ifdef _WIN32
                 // ??
#else
                 sync();
#endif
             })
#ifdef _WIN32
        .run("MapViewOfFile",
             [&] {
                 HANDLE hFile = CreateFileW(resultMmap.path.generic_wstring().c_str(),
                                            GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                            FILE_ATTRIBUTE_NORMAL, nullptr);
                 CHECK(hFile != INVALID_HANDLE_VALUE);

                 size_t        elements = numIntsToWrite;
                 size_t        size = sizeof(int32_t) * elements;
                 LARGE_INTEGER liSize;
                 liSize.QuadPart = size;
                 CHECK(SetFilePointerEx(hFile, liSize, nullptr, FILE_BEGIN));
                 CHECK(SetEndOfFile(hFile));

                 HANDLE hMap = CreateFileMappingW(hFile, nullptr, PAGE_READWRITE, 0, 0, nullptr);
                 CHECK(hMap != nullptr);

                 auto mapped = (int32_t*)MapViewOfFile(hMap, FILE_MAP_WRITE, 0, 0, size);
                 CHECK(mapped != nullptr);

                 for (size_t i = 0; i < elements; ++i)
                     mapped[i] = int32_t(i);

                 CHECK(FlushViewOfFile(mapped, size));
                 CHECK(FlushFileBuffers(hFile));
                 CHECK(UnmapViewOfFile(mapped));
                 CloseHandle(hMap);
                 CloseHandle(hFile);
             })
#else
        .run("mmap",
             [&] {
                 int    f = open(resultMmap.path.c_str(), O_RDWR | O_CREAT | O_TRUNC, (mode_t)0600);
                 size_t elements = numIntsToWrite;
                 size_t size = sizeof(int32_t) * elements;
                 CHECK(ftruncate(f, size) == 0);
                 auto mapped = (int32_t*)mmap(0, size, PROT_WRITE, MAP_SHARED, f, 0);
                 CHECK(mapped != nullptr);
                 for (size_t i = 0; i < elements; ++i)
                     mapped[i] = int32_t(i);
                 CHECK(fsync(f) == 0);
                 CHECK(munmap(mapped, size) == 0);
                 sync();
             })
#endif
        .run("writer", [&] {
            decodeless::file_writer f(resultWriter.path, 1024 * 1024 * 1024, 4);
            for (int32_t i = 0; i < numIntsToWrite; ++i)
                f.create<int32_t>(i);
#ifdef _WIN32
            // ??
#else
            sync();
#endif
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

TEST(WriteFile, SequentialBlocks) {
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
#ifdef _WIN32
                 FlushFileBuffers((HANDLE)_get_osfhandle(_fileno(f)));
                 fclose(f);
#else
                 fclose(f);
                 sync();
#endif
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
#ifdef _WIN32
                 // ??
#else
                 sync();
#endif
             })
#ifdef _WIN32
        .run("MapViewOfFile",
             [&] {
                 HANDLE hFile = CreateFileW(resultMmap.path.generic_wstring().c_str(),
                                            GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                            FILE_ATTRIBUTE_NORMAL, nullptr);
                 CHECK(hFile != INVALID_HANDLE_VALUE);

                 size_t        elements = numBlocksToWrite * numIntsPerBlock;
                 size_t        size = sizeof(int32_t) * elements;
                 LARGE_INTEGER liSize;
                 liSize.QuadPart = size;
                 CHECK(SetFilePointerEx(hFile, liSize, nullptr, FILE_BEGIN));
                 CHECK(SetEndOfFile(hFile));

                 HANDLE hMap = CreateFileMappingW(hFile, nullptr, PAGE_READWRITE, 0, 0, nullptr);
                 CHECK(hMap != nullptr);

                 auto mapped = (int32_t*)MapViewOfFile(hMap, FILE_MAP_WRITE, 0, 0, size);
                 CHECK(mapped != nullptr);

                 for (int32_t i = 0; i < (int32_t)(elements / numIntsPerBlock); ++i)
                     std::ranges::fill(
                         std::span(mapped, elements).subspan(i * numIntsPerBlock, numIntsPerBlock),
                         i);

                 CHECK(FlushViewOfFile(mapped, size));
                 CHECK(FlushFileBuffers(hFile));
                 CHECK(UnmapViewOfFile(mapped));
                 CloseHandle(hMap);
                 CloseHandle(hFile);
             })
#else
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
                 close(f);
                 sync();
             })
#endif
        .run("writer::createArray(copy)",
             [&] {
                 decodeless::file_writer f(resultWriterCopy.path, 1024 * 1024 * 1024, 4);
                 for (int32_t i = 0; i < numBlocksToWrite; ++i) {
                     std::vector<int32_t> bulk(numIntsPerBlock, i);
                     f.createArray<int32_t>(bulk);
                 }
#ifdef _WIN32
                 // ??
#else
                 sync();
#endif
             })
        .run("std::ranges::fill(writer::createArray())", [&] {
            decodeless::file_writer f(resultWriterFill.path, 1024 * 1024 * 1024, 4);
            for (int32_t i = 0; i < numBlocksToWrite; ++i)
                std::ranges::fill(f.createArray<int32_t>(numIntsPerBlock), i);
#ifdef _WIN32
            // ??
#else
            sync();
#endif
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

TEST(WriteMemory, SequentialInts) {
    constexpr size_t  reservedAddressSpace = 1024 * 1024 * 1024;
    constexpr int32_t numIntsToWrite = MB_PER_RUN * 1024 * 1024 / sizeof(int32_t);
    constexpr size_t  bytesToWrite = numIntsToWrite * sizeof(int32_t);
    static_assert(bytesToWrite <= reservedAddressSpace);
    printf("Writing %zu bytes\n", bytesToWrite);
    nb::Bench()
        .minEpochTime(std::chrono::milliseconds(50))
        .maxEpochTime(std::chrono::seconds(3))
        .minEpochIterations(3)
        //.warmup(1)
        .relative(true)
#ifdef _WIN32
        .run("VirtualAlloc",
             [&] {
                 size_t pageSize = []() {
                     SYSTEM_INFO info;
                     GetSystemInfo(&info);
                     return info.dwPageSize;
                 }();
                 uint32_t* memory =
                     (uint32_t*)VirtualAlloc(0, reservedAddressSpace, MEM_RESERVE, PAGE_NOACCESS);
                 size_t end = 0;
                 for (size_t i = 0; i < numIntsToWrite; ++i) {
                     if (i * sizeof(int32_t) >= end) {
                         size_t allocSize =
                             std::max(end, pageSize); // match writer's exponential increase
                         assert(allocSize % pageSize == 0);
                         std::ignore = VirtualAlloc(((std::byte*)memory) + end, allocSize,
                                                    MEM_COMMIT, PAGE_READWRITE);
                         end += allocSize;
                     }
                     memory[i] = int32_t(i);
                 }
                 ankerl::nanobench::doNotOptimizeAway(memory);
                 CHECK(VirtualFree(memory, 0, MEM_RELEASE));
             })
#else
        .run("mmap",
             [&] {
                 size_t pageSize = sysconf(_SC_PAGESIZE);
                 uint32_t* memory =
                     (uint32_t*)mmap(nullptr, reservedAddressSpace, PROT_NONE,
                                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
                 CHECK(memory != MAP_FAILED);
                 size_t end = 0;
                 for (size_t i = 0; i < numIntsToWrite; ++i) {
                     if (i * sizeof(int32_t) >= end) {
                         size_t allocSize =
                             std::max(end, pageSize); // match writer's exponential increase
                         assert(allocSize % pageSize == 0);
                         CHECK(mprotect((std::byte*)memory + end, allocSize,
                                        PROT_READ | PROT_WRITE) == 0);
                         end += allocSize;
                     }
                     memory[i] = int32_t(i);
                 }
                 ankerl::nanobench::doNotOptimizeAway(memory);
                 CHECK(munmap(memory, reservedAddressSpace) == 0);
             })
#endif
        .run("writer", [&] {
            decodeless::memory_writer f(reservedAddressSpace, 4);
            for (int32_t i = 0; i < numIntsToWrite; ++i)
                f.create<int32_t>(i);
        });
}

TEST(WriteMemory, SequentialBlocks) {
    constexpr size_t  reservedAddressSpace = 1024 * 1024 * 1024;
    constexpr int32_t numIntsPerBlock = 10000;
    constexpr int32_t numIntsToWrite = MB_PER_RUN * 1024 * 1024 / sizeof(int32_t);
    constexpr int32_t numBlocksToWrite = numIntsToWrite / numIntsPerBlock;
    static_assert(numBlocksToWrite > 10);
    constexpr size_t bytesToWrite = numBlocksToWrite * numIntsPerBlock * sizeof(int32_t);
    static_assert(bytesToWrite <= reservedAddressSpace);
    printf("Writing %zu bytes in %i blocks of %zu bytes\n", bytesToWrite, numBlocksToWrite,
           numIntsPerBlock * sizeof(int32_t));
    nb::Bench()
        .minEpochTime(std::chrono::milliseconds(50))
        .maxEpochTime(std::chrono::seconds(3))
        .minEpochIterations(3)
        //.warmup(1)
        .relative(true)
#ifdef _WIN32
        .run("VirtualAlloc",
             [&] {
                 size_t pageSize = []() {
                     SYSTEM_INFO info;
                     GetSystemInfo(&info);
                     return info.dwPageSize;
                 }();
                 uint32_t* memory =
                     (uint32_t*)VirtualAlloc(0, reservedAddressSpace, MEM_RESERVE, PAGE_NOACCESS);
                 size_t allocPages = (numIntsPerBlock * sizeof(int32_t) + pageSize - 1) / pageSize;
                 size_t end = 0;
                 for (int32_t i = 0; i < numBlocksToWrite; ++i) {
                     if ((i + 1) * numIntsPerBlock * sizeof(int32_t) > end) {
                         size_t allocSize = std::max(
                             end, allocPages * pageSize); // match writer's exponential increase
                         assert(allocSize % pageSize == 0);
                         std::ignore = VirtualAlloc(((std::byte*)memory) + end, allocSize,
                                                    MEM_COMMIT, PAGE_READWRITE);
                         end += allocSize;
                     }
                     std::ranges::fill(std::span(memory + i * numIntsPerBlock, numIntsPerBlock), i);
                 }
                 ankerl::nanobench::doNotOptimizeAway(memory);
                 CHECK(VirtualFree(memory, 0, MEM_RELEASE));
             })
#else
        .run("mmap",
             [&] {
                 size_t pageSize = sysconf(_SC_PAGESIZE);
                 uint32_t* memory =
                     (uint32_t*)mmap(nullptr, reservedAddressSpace, PROT_NONE,
                                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
                 CHECK(memory != MAP_FAILED);
                 size_t allocPages = (numIntsPerBlock * sizeof(int32_t) + pageSize - 1) / pageSize;
                 size_t end = 0;
                 for (int32_t i = 0; i < numBlocksToWrite; ++i) {
                     if ((i + 1) * numIntsPerBlock * sizeof(int32_t) > end) {
                         size_t allocSize = std::max(
                             end, allocPages * pageSize); // match writer's exponential increase
                         assert(allocSize % pageSize == 0);
                         CHECK(mprotect((std::byte*)memory + end, allocSize,
                                        PROT_READ | PROT_WRITE) == 0);
                         end += allocSize;
                     }
                     std::ranges::fill(std::span(memory + i * numIntsPerBlock, numIntsPerBlock), i);
                 }
                 ankerl::nanobench::doNotOptimizeAway(memory);
                 CHECK(munmap(memory, reservedAddressSpace) == 0);
             })
#endif
        .run("std::ranges::fill(writer::createArray())", [&] {
            decodeless::memory_writer f(reservedAddressSpace, 4);
            for (int32_t i = 0; i < numBlocksToWrite; ++i)
                std::ranges::fill(f.createArray<int32_t>(numIntsPerBlock), i);
        });
}

TEST(MapFile, LinearResize) {
    const TmpFile    mappedFile("tmp.dat");
    constexpr size_t reservedAddressSpace = 1024 * 1024 * 1024;
    constexpr size_t mapAddressSpace = 1024 * 1024;
    constexpr size_t stepSize = 1009;
    printf("Mapping %zu bytes in increments of %zu bytes\n", mapAddressSpace, stepSize);
    nb::Bench()
        .minEpochTime(std::chrono::milliseconds(50))
        .maxEpochTime(std::chrono::seconds(3))
        .minEpochIterations(3)
        //.warmup(1)
        .relative(true)
#ifdef _WIN32
        .run("MapViewOfFile (slow; remap whole file)",
             [&] {
                 size_t pageSize = []() {
                     SYSTEM_INFO info;
                     GetSystemInfo(&info);
                     return info.dwPageSize;
                 }();

                 HANDLE hFile = CreateFileW(mappedFile.path.generic_wstring().c_str(),
                                            GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                            FILE_ATTRIBUTE_NORMAL, nullptr);
                 CHECK(hFile != INVALID_HANDLE_VALUE);
                 size_t mappedBytes = 0;
                 size_t mappedPages = 0;
                 HANDLE hMap = nullptr;
                 LPVOID mapped = nullptr;
                 for (; mappedBytes + stepSize <= mapAddressSpace;) {
                     if (mappedBytes + stepSize > mappedPages * pageSize) {
                         mappedPages++;
                         if (mapped) {
                             CHECK(UnmapViewOfFile(mapped));
                             CloseHandle(hMap);
                         }
                         {
                             LARGE_INTEGER liSize;
                             liSize.QuadPart = mappedPages * pageSize;
                             CHECK(SetFilePointerEx(hFile, liSize, nullptr, FILE_BEGIN));
                             CHECK(SetEndOfFile(hFile));
                         }
                         hMap = CreateFileMappingW(hFile, nullptr, PAGE_READWRITE, 0, 0, nullptr);
                         CHECK(hMap != nullptr);
                         mapped = MapViewOfFile(hMap, FILE_MAP_WRITE, 0, 0, mappedPages * pageSize);
                         CHECK(mapped != nullptr);
                         ankerl::nanobench::doNotOptimizeAway(mapped);
                     }
                     mappedBytes += stepSize;
                     ankerl::nanobench::doNotOptimizeAway(mappedBytes);
                 }
                 CHECK(UnmapViewOfFile(mapped));

                 CloseHandle(hMap);
                 CloseHandle(hFile);
             })
        .run("MapViewOfFile (cheat: whole file up-front)",
             [&] {
                 size_t pageSize = []() {
                     SYSTEM_INFO info;
                     GetSystemInfo(&info);
                     return info.dwPageSize;
                 }();

                 HANDLE hFile = CreateFileW(mappedFile.path.generic_wstring().c_str(),
                                            GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                            FILE_ATTRIBUTE_NORMAL, nullptr);
                 CHECK(hFile != INVALID_HANDLE_VALUE);

                 LARGE_INTEGER liSize;
                 liSize.QuadPart = mapAddressSpace;
                 CHECK(SetFilePointerEx(hFile, liSize, nullptr, FILE_BEGIN));
                 CHECK(SetEndOfFile(hFile));
                 HANDLE hMap = CreateFileMappingW(hFile, nullptr, PAGE_READWRITE, 0, 0, nullptr);

                 CHECK(hMap != nullptr);
                 size_t mappedBytes = 0;
                 size_t mappedPages = 0;
                 LPVOID mapped = nullptr;
                 for (; mappedBytes + stepSize <= mapAddressSpace;) {
                     if (mappedBytes + stepSize > mappedPages * pageSize) {
                         mappedPages++;
                         if (mapped)
                             CHECK(UnmapViewOfFile(mapped));
                         mapped = MapViewOfFile(hMap, FILE_MAP_WRITE, 0, 0, mappedPages * pageSize);
                         CHECK(mapped != nullptr);
                         ankerl::nanobench::doNotOptimizeAway(mapped);
                     }
                     mappedBytes += stepSize;
                     ankerl::nanobench::doNotOptimizeAway(mappedBytes);
                 }
                 CHECK(UnmapViewOfFile(mapped));

                 CloseHandle(hMap);
                 CloseHandle(hFile);
             })
#else
        .run("mmap",
             [&] {
                 size_t pageSize = sysconf(_SC_PAGESIZE);
                 void* reserved = mmap(nullptr, reservedAddressSpace, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
                 int    f = open(mappedFile.path.c_str(), O_RDWR | O_CREAT | O_TRUNC, (mode_t)0600);
                 size_t mappedBytes = 0;
                 size_t mappedPages = 0;
                 void*  mappedEnd = reserved;
                 for (; mappedBytes + stepSize <= mapAddressSpace;) {
                     if (mappedBytes + stepSize > mappedPages * pageSize) {
                         size_t allocPages =
                             (mappedBytes + stepSize + pageSize - 1) / pageSize - mappedPages;
                         CHECK(ftruncate(f, mappedPages * pageSize) == 0);
                         auto mapped =
                             mmap(mappedEnd,
                                            allocPages * pageSize,
                                            PROT_WRITE,
                                                      MAP_SHARED, f, 0);
                         CHECK(mapped != nullptr);
                         mappedEnd = (std::byte*)mapped + allocPages * pageSize;
                         mappedPages += allocPages;
                         ankerl::nanobench::doNotOptimizeAway(mapped);
                     }
                     mappedBytes += stepSize;
                     ankerl::nanobench::doNotOptimizeAway(mappedBytes);
                 }
                 CHECK(munmap(reserved, reservedAddressSpace) == 0);
                 close(f);
             })
#endif
        .run("resizable_file", [&] {
            decodeless::resizable_file m(mappedFile.path, reservedAddressSpace);
            for (; m.size() + stepSize <= mapAddressSpace;) {
                m.resize(m.size() + stepSize);
                ankerl::nanobench::doNotOptimizeAway(m.size());
                ankerl::nanobench::doNotOptimizeAway(m.data());
            }
        });
}

TEST(MapFile, LinearResizePages) {
    const TmpFile    mappedFile("tmp.dat");
    constexpr size_t reservedAddressSpace = 1024 * 1024 * 1024;
    constexpr size_t mapAddressSpace = 1024 * 1024;
#ifdef _WIN32
    size_t pageSize = []() {
        SYSTEM_INFO info;
        GetSystemInfo(&info);
        return info.dwPageSize;
    }();
#else
    size_t pageSize = sysconf(_SC_PAGESIZE);
#endif
    printf("Mapping %zu bytes in increments of %zu bytes\n", mapAddressSpace, pageSize);
    nb::Bench()
        .minEpochTime(std::chrono::milliseconds(50))
        .maxEpochTime(std::chrono::seconds(3))
        .minEpochIterations(3)
        //.warmup(1)
        .relative(true)
#ifdef _WIN32
        .run("MapViewOfFile (slow; remap whole file)",
             [&] {
                 size_t pageSize = []() {
                     SYSTEM_INFO info;
                     GetSystemInfo(&info);
                     return info.dwPageSize;
                 }();

                 HANDLE hFile = CreateFileW(mappedFile.path.generic_wstring().c_str(),
                                            GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                            FILE_ATTRIBUTE_NORMAL, nullptr);
                 CHECK(hFile != INVALID_HANDLE_VALUE);
                 for (size_t pages = 1; pages * pageSize <= mapAddressSpace; ++pages) {
                     LARGE_INTEGER liSize;
                     liSize.QuadPart = pages * pageSize;
                     CHECK(SetFilePointerEx(hFile, liSize, nullptr, FILE_BEGIN));
                     CHECK(SetEndOfFile(hFile));
                     HANDLE hMap =
                         CreateFileMappingW(hFile, nullptr, PAGE_READWRITE, 0, 0, nullptr);
                     CHECK(hMap != nullptr);
                     auto mapped =
                         (int32_t*)MapViewOfFile(hMap, FILE_MAP_WRITE, 0, 0, pages * pageSize);
                     CHECK(mapped != nullptr);
                     ankerl::nanobench::doNotOptimizeAway(mapped);
                     CHECK(UnmapViewOfFile(mapped));
                     CloseHandle(hMap);
                 }
                 CloseHandle(hFile);
             })
        .run("MapViewOfFile (cheat: whole file up-front)",
             [&] {
                 size_t pageSize = []() {
                     SYSTEM_INFO info;
                     GetSystemInfo(&info);
                     return info.dwPageSize;
                 }();

                 HANDLE hFile = CreateFileW(mappedFile.path.generic_wstring().c_str(),
                                            GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                            FILE_ATTRIBUTE_NORMAL, nullptr);
                 CHECK(hFile != INVALID_HANDLE_VALUE);

                 LARGE_INTEGER liSize;
                 liSize.QuadPart = mapAddressSpace;
                 CHECK(SetFilePointerEx(hFile, liSize, nullptr, FILE_BEGIN));
                 CHECK(SetEndOfFile(hFile));
                 HANDLE hMap = CreateFileMappingW(hFile, nullptr, PAGE_READWRITE, 0, 0, nullptr);
                 CHECK(hMap != nullptr);

                 for (size_t pages = 1; pages * pageSize <= mapAddressSpace; ++pages) {
                     auto mapped =
                         (int32_t*)MapViewOfFile(hMap, FILE_MAP_WRITE, 0, 0, pages * pageSize);
                     CHECK(mapped != nullptr);
                     ankerl::nanobench::doNotOptimizeAway(mapped);
                     CHECK(UnmapViewOfFile(mapped));
                 }

                 CloseHandle(hMap);
                 CloseHandle(hFile);
             })
#else
        .run("mmap",
             [&] {
                 size_t pageSize = sysconf(_SC_PAGESIZE);
                 void* reserved = mmap(nullptr, reservedAddressSpace, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
                 int    f = open(mappedFile.path.c_str(), O_RDWR | O_CREAT | O_TRUNC, (mode_t)0600);
                 size_t pages;
                 for (pages = 1; pages * pageSize <= mapAddressSpace; ++pages) {
                     CHECK(ftruncate(f, pages * pageSize) == 0);
                     auto mapped = (int32_t*)mmap((std::byte*)reserved + (pages - 1) * pageSize, pageSize, PROT_WRITE,
                                                  MAP_SHARED, f, 0);
                     ankerl::nanobench::doNotOptimizeAway(mapped);
                 }
                 CHECK(fsync(f) == 0);
                 CHECK(munmap(reserved, reservedAddressSpace) == 0);
             })
#endif
        .run("resizable_file", [&] {
            decodeless::resizable_file m(mappedFile.path, reservedAddressSpace);
            for (; m.size() + pageSize <= mapAddressSpace;) {
                m.resize(m.size() + pageSize);
                ankerl::nanobench::doNotOptimizeAway(m.size());
                ankerl::nanobench::doNotOptimizeAway(m.data());
            }
        });
}

TEST(MapMemory, LinearResize) {
    constexpr size_t reservedAddressSpace = 1024 * 1024 * 1024;
    constexpr size_t mapAddressSpace = 1024 * 1024;
    constexpr size_t stepSize = 1009;
    printf("Mapping %zu bytes in increments of %zu bytes\n", mapAddressSpace, stepSize);
    nb::Bench()
        .minEpochTime(std::chrono::milliseconds(1000))
        .maxEpochTime(std::chrono::seconds(3))
        .minEpochIterations(3)
        //.warmup(1)
        .relative(true)
#ifdef _WIN32
        .run("VirtualAlloc",
             [&] {
                 size_t pageSize = []() {
                     SYSTEM_INFO info;
                     GetSystemInfo(&info);
                     return info.dwPageSize;
                 }();
                 uint32_t* reserved =
                     (uint32_t*)VirtualAlloc(0, reservedAddressSpace, MEM_RESERVE, PAGE_NOACCESS);
                 size_t mappedBytes = 0;
                 size_t mappedPages = 0;
                 for (; mappedBytes + stepSize <= mapAddressSpace;) {
                     if (mappedBytes + stepSize > mappedPages * pageSize) {
                         size_t allocPages =
                             (mappedBytes + stepSize + pageSize - 1) / pageSize - mappedPages;
                         std::ignore =
                             VirtualAlloc(((std::byte*)reserved) + mappedPages * pageSize,
                                          allocPages * pageSize, MEM_COMMIT, PAGE_READWRITE);
                         mappedPages += allocPages;
                     }
                     mappedBytes += stepSize;
                     // do nothing with the memory
                     ankerl::nanobench::doNotOptimizeAway(mappedBytes);
                     ankerl::nanobench::doNotOptimizeAway(reserved);
                 }
                 CHECK(VirtualFree(reserved, 0, MEM_RELEASE));
             })
#else
        .run("mmap",
             [&] {
                 size_t pageSize = sysconf(_SC_PAGESIZE);
                 uint32_t* reserved =
                     (uint32_t*)mmap(nullptr, reservedAddressSpace, PROT_NONE,
                                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
                 CHECK(reserved != MAP_FAILED);
                 size_t mappedBytes = 0;
                 size_t mappedPages = 0;
                 for (; mappedBytes + stepSize <= mapAddressSpace;) {
                     if (mappedBytes + stepSize > mappedPages * pageSize) {
                         size_t allocPages =
                             (mappedBytes + stepSize + pageSize - 1) / pageSize - mappedPages;
                         CHECK(mprotect((std::byte*)reserved + mappedPages * pageSize,
                                        allocPages * pageSize,
                                        PROT_READ | PROT_WRITE) == 0);
                         mappedPages += allocPages;
                     }
                     mappedBytes += stepSize;
                     // do nothing with the memory
                     ankerl::nanobench::doNotOptimizeAway(mappedBytes);
                     ankerl::nanobench::doNotOptimizeAway(reserved);
                 }
                 CHECK(munmap(reserved, reservedAddressSpace) == 0);
             })
#endif
        .run("resizable_memory", [&] {
            decodeless::resizable_memory m(0, reservedAddressSpace);
            for (; m.size() + stepSize <= mapAddressSpace;) {
                m.resize(m.size() + stepSize);
                ankerl::nanobench::doNotOptimizeAway(m.size());
                ankerl::nanobench::doNotOptimizeAway(m.data());
            }
        });
}

TEST(MapMemory, LinearResizePages) {
    constexpr size_t reservedAddressSpace = 1024 * 1024 * 1024;
    constexpr size_t mapAddressSpace = 1024 * 1024;
#ifdef _WIN32
    size_t pageSize = []() {
        SYSTEM_INFO info;
        GetSystemInfo(&info);
        return info.dwPageSize;
    }();
#else
    size_t pageSize = sysconf(_SC_PAGESIZE);
#endif
    printf("Mapping %zu bytes in increments of %zu bytes\n", mapAddressSpace, pageSize);
    nb::Bench()
        .minEpochTime(std::chrono::milliseconds(1000))
        .maxEpochTime(std::chrono::seconds(3))
        .minEpochIterations(3)
        //.warmup(1)
        .relative(true)
#ifdef _WIN32
        .run("VirtualAlloc",
             [&] {
                 size_t pageSize = []() {
                     SYSTEM_INFO info;
                     GetSystemInfo(&info);
                     return info.dwPageSize;
                 }();
                 uint32_t* reserved =
                     (uint32_t*)VirtualAlloc(0, reservedAddressSpace, MEM_RESERVE, PAGE_NOACCESS);
                 size_t mappedPages = 0;
                 for (; mappedPages * pageSize <= mapAddressSpace;) {
                     std::ignore = VirtualAlloc(((std::byte*)reserved) + mappedPages * pageSize,
                                                pageSize, MEM_COMMIT, PAGE_READWRITE);
                     mappedPages++;
                     // do nothing with the memory
                     ankerl::nanobench::doNotOptimizeAway(mappedPages);
                     ankerl::nanobench::doNotOptimizeAway(reserved);
                 }
                 CHECK(VirtualFree(reserved, 0, MEM_RELEASE));
             })
#else
        .run("mmap",
             [&] {
                 size_t pageSize = sysconf(_SC_PAGESIZE);
                 uint32_t* reserved =
                     (uint32_t*)mmap(nullptr, reservedAddressSpace, PROT_NONE,
                                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
                 CHECK(reserved != MAP_FAILED);
                 size_t mappedPages = 0;
                 for (; mappedPages * pageSize <= mapAddressSpace;) {
                     CHECK(mprotect((std::byte*)reserved + mappedPages * pageSize, pageSize,
                                    PROT_READ | PROT_WRITE) == 0);
                     mappedPages++;
                     // do nothing with the memory
                     ankerl::nanobench::doNotOptimizeAway(mappedPages);
                     ankerl::nanobench::doNotOptimizeAway(reserved);
                 }
                 CHECK(munmap(reserved, reservedAddressSpace) == 0);
             })
#endif
        .run("resizable_memory", [&] {
            decodeless::resizable_memory m(0, reservedAddressSpace);
            for (; m.size() + pageSize <= mapAddressSpace;) {
                m.resize(m.size() + pageSize);
                ankerl::nanobench::doNotOptimizeAway(m.size());
                ankerl::nanobench::doNotOptimizeAway(m.data());
            }
        });
}
