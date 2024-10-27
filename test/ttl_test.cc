

#include "gtest/gtest.h"

#include "leveldb/env.h"
#include "leveldb/db.h"
#include <unordered_set>

using namespace leveldb;

constexpr int value_size = 2048;
constexpr int data_size = 128 << 20;

Status OpenDB(std::string dbName, DB **db) {
  Options options;
  options.create_if_missing = true;
  return DB::Open(options, dbName, db);
}

void InsertData(DB *db, uint64_t ttl/* second */) {
  WriteOptions writeOptions;
  int key_num = data_size / value_size;
  srand(42);

  // 用于存储成功写入的唯一键
  std::unordered_set<std::string> unique_keys;

  for (int i = 0; i < key_num; i++) {
    int key_ = rand() % key_num+1;
    std::string key = std::to_string(key_);
    std::string value(value_size, 'a');
    Status status = db->Put(writeOptions, key, value, ttl);
    if (!status.ok()) {
        // 输出失败的状态信息并退出循环
        std::cerr << "Failed to write key: " << key 
                  << ", Status: " << status.ToString() << std::endl;
    }else{
        std::cerr << "Success to write key: " << key << std::endl;
        unique_keys.insert(key);  // 插入集合中，如果已经存在则不会重复插入
    }
  }

  // 打印成功写入的唯一键的数量
    std::cout << "Total unique keys successfully written: " << unique_keys.size() << std::endl;
}

void GetData(DB *db, int size = (1 << 30)) {
  ReadOptions readOptions;
  int key_num = data_size / value_size;
  
  // 点查
  srand(42);
  for (int i = 0; i < 100; i++) {
    int key_ = rand() % key_num+1;
    std::string key = std::to_string(key_);
    std::string value;
    db->Get(readOptions, key, &value);
  }
}

TEST(TestTTL, ReadTTL) {
    DB *db;
    if(OpenDB("testdb", &db).ok() == false) {
        std::cerr << "open db failed" << std::endl;
        abort();
    }

    uint64_t ttl = 20;

    InsertData(db, ttl);

    ReadOptions readOptions;
    Status status;
    int key_num = data_size / value_size;
    srand(42);
    for (int i = 0; i < 100; i++) {
        int key_ = rand() % key_num+1;
        std::string key = std::to_string(key_);
        std::string value;
        status = db->Get(readOptions, key, &value);

        // 检查 status 并打印出失败的状态信息
        if (!status.ok()) {
            std::cerr << "Key: " << key << ", Status: " << status.ToString() << std::endl;
        }
        
        ASSERT_TRUE(status.ok());
    }

    Env::Default()->SleepForMicroseconds(ttl * 1000000);

    for (int i = 0; i < 100; i++) {
        int key_ = rand() % key_num+1;
        std::string key = std::to_string(key_);
        std::string value;
        status = db->Get(readOptions, key, &value);

        // 检查 status 并打印出失败的状态信息
        if (status.ok()) {
            std::cerr << "Key: " << key << ", Status: " << status.ToString() << std::endl;
        }

        ASSERT_FALSE(status.ok());
    }
}

TEST(TestTTL, CompactionTTL) {
    DB *db;

    if(OpenDB("testdb", &db).ok() == false) {
        std::cerr << "open db failed" << std::endl;
        abort();
    }

    uint64_t ttl = 20;
    InsertData(db, ttl);

    leveldb::Range ranges[1];
    ranges[0] = leveldb::Range("-", "A");
    uint64_t sizes[1];
    db->GetApproximateSizes(ranges, 1, sizes);
    ASSERT_GT(sizes[0], 0);

    Env::Default()->SleepForMicroseconds(ttl * 1000000);

    db->CompactRange(nullptr, nullptr);

    ranges[0] = leveldb::Range("-", "A");
    db->GetApproximateSizes(ranges, 1, sizes);
    ASSERT_EQ(sizes[0], 0);
}


int main(int argc, char** argv) {
  srand(42);
  // All tests currently run with the same read-only file limits.
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
