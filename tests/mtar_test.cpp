#include <gtest/gtest.h>
#include <dirent.h>
#include <stdint.h>

#include "microtar.h"

#define TEST_ITER 10000
#define TEST_FILE_SIZE_1K (1 << 10)
#define TEST_FILE_SIZE_1M (1 << 20)

#define TEST_DATA_PATH "tmp/"
#define SMALL_TAR_PATH "tmp/small.tar"
#define BIG_TAR_PATH "tmp/big.tar"

#if defined(_WIN32)

#include <windows.h>
#define mkdir(path, opts) !CreateDirectory(path, NULL)

#endif

void write_tar(const char *tar_path, uint32_t size) {
    char test_file[32];
    char *test_str;
    DIR *dir;
    mtar_t tar;
    uint32_t i;

    test_str = (char *)malloc(size);
    ASSERT_NE(test_str, nullptr);

    for (i = 0; i < size; i++) {
        test_str[i] = 'a';
    }

    dir = opendir(TEST_DATA_PATH);

    if (!dir && errno == ENOENT) {
        /* dir doesn't exist, so make it */
        EXPECT_EQ(mkdir(TEST_DATA_PATH, 0777), 0);
    }

    EXPECT_EQ(mtar_open(&tar, tar_path, "w"), MTAR_ESUCCESS);

    for (i = 0; i < TEST_ITER; i++) {
        snprintf(test_file, sizeof(test_file), TEST_DATA_PATH "%d.txt", i);

        EXPECT_EQ(mtar_write_file_header(&tar, test_file, size),
                  MTAR_ESUCCESS);
        EXPECT_EQ(mtar_write_data(&tar, test_str, size),
                  MTAR_ESUCCESS);
    }

    EXPECT_EQ(mtar_finalize(&tar), MTAR_ESUCCESS);
    EXPECT_EQ(mtar_close(&tar), MTAR_ESUCCESS);

    for (i = 0; i < TEST_ITER; i++) {
        snprintf(test_file, sizeof(test_file), TEST_DATA_PATH "%d.txt", i);
        remove(test_file);
    }

    free(test_str);
    closedir(dir);
}

void read_tar(const char *tar_path) {
    mtar_t tar;
    mtar_header_t h;
    char *p;
    uint32_t found = 0;

    EXPECT_EQ(mtar_open(&tar, tar_path, "r"), MTAR_ESUCCESS);

    while (mtar_read_header(&tar, &h) != MTAR_ENULLRECORD) {
        found++;

        p = (char *)malloc(h.size + 1);
        ASSERT_NE(p, nullptr);

        EXPECT_EQ(mtar_read_data(&tar, p, h.size), MTAR_ESUCCESS);

        free(p);
        EXPECT_EQ(mtar_next(&tar), MTAR_ESUCCESS);
    }

    EXPECT_EQ(mtar_close(&tar), MTAR_ESUCCESS);
    EXPECT_EQ(found, TEST_ITER);
}

void find_tar(const char*tar_path) {
    char test_file[32];
    mtar_t tar;
    mtar_header_t h;
    uint32_t i, rounds = 100;

    srand(time(NULL));

    EXPECT_EQ(mtar_open(&tar, tar_path, "r"), MTAR_ESUCCESS);

    for (i = 0; i < rounds; i++) {
        snprintf(test_file, sizeof(test_file), TEST_DATA_PATH
                 "%d.txt", rand() % TEST_ITER);

        EXPECT_EQ(mtar_find(&tar, test_file, &h), MTAR_ESUCCESS);
    }

    EXPECT_EQ(mtar_close(&tar), MTAR_ESUCCESS);
}

void cleanup() {
    char test_file[32];
    uint32_t i;

    for (i = 0; i < TEST_ITER; i++) {
        snprintf(test_file, sizeof(test_file), TEST_DATA_PATH "%d.txt", i);
        remove(test_file);
    }

    remove(SMALL_TAR_PATH);
    remove(BIG_TAR_PATH);
}

TEST(microtar, mtar_write_1K) {
    write_tar(SMALL_TAR_PATH, TEST_FILE_SIZE_1K);
}

TEST(microtar, mtar_read1K) {
    read_tar(SMALL_TAR_PATH);
}

TEST(microtar, mtar_find_1K) {
    find_tar(SMALL_TAR_PATH);
}

TEST(microtar, mtar_write_1M) {
    write_tar(BIG_TAR_PATH, TEST_FILE_SIZE_1M);
}

TEST(microtar, mtar_read_1M) {
    read_tar(BIG_TAR_PATH);
}

TEST(microtar, mtar_find_1M) {
    find_tar(BIG_TAR_PATH);
}

TEST(microtar, mtar_cleanup) {
    cleanup();
}
