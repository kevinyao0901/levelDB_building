

#include "gtest/gtest.h"

#include "leveldb/env.h"
#include "leveldb/db.h"
#include <unordered_set>

using namespace leveldb;

constexpr int value_size = 2048;
constexpr int data_size = 128 << 20;

//--------------------------------------------------------------
void PrintAllKeys(DB *db) {
    // 创建一个读选项对象
    ReadOptions readOptions;

    int LeftKeyCount = 0;

    // 创建迭代器
    std::unique_ptr<Iterator> it(db->NewIterator(readOptions));

    // 遍历所有键
    for (it->SeekToFirst(); it->Valid(); it->Next()) {
        // std::string key = it->key().ToString();
        // std::string value = it->value().ToString();
        // std::cout << "Key: " << key << std::endl;
        LeftKeyCount++;
    }

    // 检查迭代器的有效性
    if (!it->status().ok()) {
        std::cerr << "Error iterating through keys: " << it->status().ToString() << std::endl;
    }
    std::cerr << "Key hasn't been deleted: " << LeftKeyCount << std::endl;
}

//------------------------------------------------------------------

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
        std::string key;
        do {
            int key_ = rand() % key_num + 1;
            key = std::to_string(key_);
        } while (unique_keys.find(key) != unique_keys.end()); // 检查是否已存在

        std::string value(value_size, 'a');

        // 判断 key 是否在范围内
        if (key >= "-" && key < "A") {
            //std::cout << "Key: " << key << " is within the range (-, A)" << std::endl;
        } else {
            std::cout << "Key: " << key << " is outside the range (-, A)" << std::endl;
            return;
        }

        Status status = db->Put(writeOptions, key, value, ttl);
        if (!status.ok()) {
            // 输出失败的状态信息并退出循环
            std::cerr << "Failed to write key: " << key 
                      << ", Status: " << status.ToString() << std::endl;
        } else {
            unique_keys.insert(key);  // 插入集合中，如果已经存在则不会重复插入
        }
    }

    Iterator* iter = db->NewIterator(ReadOptions());
    iter->SeekToFirst();
    std::cout << "Data base First key: " << iter->key().ToString() << std::endl;
    delete iter;

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

    Iterator* iter = db->NewIterator(ReadOptions());
    iter->SeekToFirst();
    std::cout << "Data base First key: " << iter->key().ToString() << std::endl;
    int cnt = 0;
    while (iter->Valid())
    {
        cnt++;
        iter->Next();
    }
    std::cout << "Total key cnt: " << cnt << "\n";
    delete iter;

}

#if 0
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

    srand(42);
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

    delete db;
}

#endif

TEST(TestTTL, CompactionTTL) {
    DB *db;

    leveldb::Options options;
    // options.write_buffer_size = 1024*1024*1024;
    // options.max_file_size = 1024*1024*1024;
    leveldb::DestroyDB("testdb", options);

    if(OpenDB("testdb", &db).ok() == false) {
        std::cerr << "open db failed" << std::endl;
        abort();
    }

    uint64_t ttl = 20;

    leveldb::Range ranges[1];
    ranges[0] = leveldb::Range("-", "A");
    uint64_t sizes[1];
    db->GetApproximateSizes(ranges, 1, sizes);
    ASSERT_EQ(sizes[0], 0);

    InsertData(db, ttl);

    //leveldb::Range ranges[1];
    ranges[0] = leveldb::Range("-", "A");
    //uint64_t sizes[1];
    db->GetApproximateSizes(ranges, 1, sizes);
    ASSERT_GT(sizes[0], 0);


    ttl += 10;
    Env::Default()->SleepForMicroseconds(ttl * 1000000);

    std::cout << "Start drop\n";
    db->CompactRange(nullptr, nullptr);

    ranges[0] = leveldb::Range("-", "A");
    db->GetApproximateSizes(ranges, 1, sizes);
    PrintAllKeys(db);
    ASSERT_EQ(sizes[0], 0);

    delete db;
}


int main(int argc, char** argv) {
  srand(42);
  // All tests currently run with the same read-only file limits.
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
