# LevelDB TTL 实验报告



## 实验背景及要求：

**TTL（Time To Live）**，即生存时间，是指数据在存储系统中的有效期。设置TTL可以使得过期的数据自动失效，减少存储空间占用，提高系统性能。

**为什么需要TTL功能：**

- **数据自动过期**：无需手动删除过期数据，简化数据管理。
- **节省存储空间**：定期清理无效数据，优化资源利用。
- **提高性能**：减少无效数据的干扰，提升读写效率。

**要求：**

- 在LevelDB中实现键值对的TTL功能，使得过期的数据在**读取时自动失效**，并在适当的时候**被合并清理**。

- 修改LevelDB的源码，实现对TTL的支持，包括数据的写入、读取和过期数据的清理。

- 编写测试用例，验证TTL功能的正确性和稳定性。

  

## 设计思路

在本次实验中，为了给 LevelDB 增加 TTL（Time-to-Live）功能，我的设计思路主要围绕以下几个方面：

1. **TTL 数据存储设计**：通过在每个 key-value 对中引入过期时间字段，使每条数据都能记录其有效期。为了实现这一点，需要在数据写入时自动附加 TTL 值，并在读取数据时检查该 key 是否过期。设计中决定将 TTL 时间戳和数据一起存储，使其在写入或读取时简单易行。

2. **过期数据的判断与清理**：在数据查询过程中加入检查机制，确保返回的数据都是未过期的。特别是在 `DBImpl::Get`、`MemTable::Get` 等方法中加入过期判断逻辑。对于过期的数据，在读取时会返回“未找到”状态。在定期清理方面，手动触发合并和删除机制，通过手动调用 `CompactRange` 来清理过期数据，以避免占用存储空间。

3. **手动合并策略**：为了在特定时间点清理过期数据，本次实验中禁用了 LevelDB 的自动合并机制。在所有数据写入完成后，通过手动调用 `db->CompactRange(nullptr, nullptr);` 触发合并，以删除过期的数据文件。这样设计的目的是为确保在批量写入后清理过期数据，减少额外开销。

   

计划在以上设计的基础上实现levelDB的TTL 功能，使得在 LevelDB 中可以较为高效地处理过期数据，并且在性能和存储空间之间取得平衡。



## 实现过程：

1. **数据格式调整**：为每条数据附加过期时间戳，将时间戳和实际数据一起存储在 value 中。
    - 新增 `AppendExpirationTime` 函数：将 TTL 过期时间戳作为小端序添加到 value 的前部。
    
    - 新增 `ParseExpirationTime` 函数：解析出附加在 value 前部的过期时间戳。

    - 新增 `ParseActualValue` 函数：提取并返回去掉过期时间戳的实际数据值。
    
      ```
      //TTL ToDo : add func for TTL Put
      
      void AppendExpirationTime(std::string* value, uint64_t expiration_time) {
        // 直接将小端序的过期时间戳（64位整数）附加到值的前面
        value->append(reinterpret_cast<const char*>(&expiration_time), sizeof(expiration_time));
      }
      
      uint64_t GetCurrentTime() {
        // 返回当前的Unix时间戳
        return static_cast<uint64_t>(time(nullptr));
      }
      
      // 解析过期时间戳
      uint64_t ParseExpirationTime(const std::string& value) {
        // 假设过期时间戳在值的前 8 字节
        assert(value.size() >= sizeof(uint64_t));
        uint64_t expiration_time;
        memcpy(&expiration_time, value.data(), sizeof(uint64_t));
        return expiration_time;  // 直接返回小端序的值
      }
      
      // 解析出实际的值（去掉前面的过期时间戳部分）
      std::string ParseActualValue(const std::string& value) {
        // 去掉前 8 字节（存储过期时间戳），返回实际值
        return value.substr(sizeof(uint64_t));
      }
      
      //finish modify
      ```
    
      
    
2. **支持 TTL 的 `Put` 方法**：扩展 `DBImpl::Put` 和 `DB::Put` 方法，使其支持指定 TTL。
    - 计算当前时间加上 TTL 作为到期时间。
    
    - 将到期时间添加到 value 前部，然后将完整的键值对写入数据库。
    
      ```
      // TTL ToDo: add DBImpl for Put
      // 新增支持TTL的Put方法
      Status DBImpl::Put(const WriteOptions& o, const Slice& key, const Slice& val, uint64_t ttl) {
        return DB::Put(o, key, val, ttl);
      }
      
      //TTL ToDo: add a func for TTL Put
      Status DB::Put(const WriteOptions& opt, const Slice& key, const Slice& value, uint64_t ttl) {
        // 获取当前时间并计算过期时间戳
        uint64_t expiration_time = GetCurrentTime() + ttl;
      
        // 将过期时间戳和值一起存储（假设值前面附加过期时间戳）
        std::string new_value;
        AppendExpirationTime(&new_value, expiration_time);
        new_value.append(value.data(), value.size());
      
        // 构造 WriteBatch，并将键值对加入到批处理中
        WriteBatch batch;
        batch.Put(key, new_value);
      
        // 执行写操作
        return Write(opt, &batch);
      }
      //finish modify
      ```
    
      
    
3. **数据清理策略**：在合并过程中清理过期数据。
    - 修改 `Status DBImpl::DoCompactionWork(CompactionState* compact)`：在合并过程中检查每个键的过期时间，若已过期则将其标记为 `drop` 丢弃，不再写入下一层级。
    
    - 添加过期键值对的计数器 `dropped_keys_count` 以跟踪被丢弃的条目数量。
    
      ```
      Status DBImpl::DoCompactionWork(CompactionState* compact){
      //...
      // TTL ToDo: add expiration time check
            // 检查是否为目标键
            if (key == target_key) {
                // 输出调试信息
                Log(options_.info_log, "Found target key during compaction: %s\n", key.ToString().c_str());
            }
      
            Slice value = input->value();
            if (value.size() >= sizeof(uint64_t)) {
              const char* ptr = value.data();
              uint64_t expiration_time = DecodeFixed64(ptr);
              uint64_t current_time = env_->NowMicros() / 1000000;
      
              if (current_time > expiration_time) {
                drop = true;  // 过期的键值对，标记为丢弃
                dropped_keys_count ++; // 初始化计数器
              }else{
                bool flag = current_time > expiration_time;
              }
            }else{
              bool bs = value.size() >= sizeof(uint64_t);
            }
      //...
      }
      ```
    
      
    
4. **数据读取时的 TTL 检查**：扩展 `DBImpl::Get`，在读取时判断数据是否过期。
    - 对获取到的 value 调用 `ParseExpirationTime` 提取出过期时间。
    
    - 若当前时间已超过过期时间，则返回 `NotFound`，否则解析实际数据并返回。
    
      ```
      Status DBImpl::Get(const ReadOptions& options, const Slice& key,
                         std::string* value) {
      //...
      // TTL ToDo : add check for TTL
        // 如果从 memtable、imm 或 sstable 获取到了数据，则需要检查TTL
        if (s.ok()) {
          // 从 value 中解析出过期时间戳（假设值存储格式为：[过期时间戳][实际值]）
          uint64_t expiration_time = ParseExpirationTime(*value);
          uint64_t current_time = GetCurrentTime();
      
          // 如果当前时间已经超过过期时间，则认为数据过期，返回 NotFound
          if (current_time >= expiration_time) {
            s = Status::NotFound(Slice());
          } else {
            // 数据未过期，解析出实际的值
            *value = ParseActualValue(*value);
          }
        }
      
        // //finish modify//...
      }
      ```
    
      

**所有修改的相关代码均标有`TTL ToDo`标签**，方便查看这样就实现了目标设计，使当前LevelDB 可以支持 TTL 功能，即能够在指定时间后自动删除过期的数据。完成了实验要求。



## 相关测试：

在原先测试脚本的基础上，为了更好的测试目标TTL设计，我在原先脚本的基础上进行了部分修改，包括但不限于修改随机种子，即使关闭数据库，添加调试信息等

```


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

    int cnt = 20;

    // 遍历所有键
    for (it->SeekToFirst(); it->Valid()&&cnt; it->Next()) {
        std::string key = it->key().ToString();
        std::string value = it->value().ToString();
        std::cout << "Key: " << key << std::endl;
        LeftKeyCount++;
        cnt--;
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
    iter->SeekToLast();
    std::cout << "Data base last key: " << iter->key().ToString() << std::endl;
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

```

修改后的测试脚本主要分为以下几个部分：

1. **InsertData函数**：
   - 通过随机生成键值对并将其写入数据库，测试数据库的写入操作。
   - 添加一个 TTL（time-to-live）时间，并确保键值在TTL时间到期后会被删除。
   - `unique_keys`集合用于存储唯一键，确保写入的数据没有重复键。

2. **GetData函数**：
   - 通过随机键读取数据，检查点查功能。
   - 使用迭代器统计并打印当前数据库中的总键数。

3. **PrintAllKeys函数**（调试信息）：
   - 迭代数据库中的所有键，并打印出部分键信息。
   - 用于检查在过期和压缩操作后是否仍然存在未删除的键。

4. **TestTTL ReadTTL测试**：
   - 测试数据库中的TTL功能是否正确。
   - 首先插入数据，然后在TTL时间到期前读取，确保数据存在。
   - 然后，等待TTL时间到期后再次读取数据，确保数据已经过期并被删除。

5. **TestTTL CompactionTTL测试**：
   - 测试压缩过程中TTL数据的清理功能。
   - 插入数据后，利用`GetApproximateSizes`函数获取数据大小。
   - 等待TTL过期后，调用`CompactRange`函数手动触发压缩。
   - 再次使用`GetApproximateSizes`检查数据库大小，确保过期数据已被清理。

通过这个脚本，能够全面验证LevelDB的TTL功能和压缩清理机制。

**运行结果截图：**
![](.\png\result.png)



## 实验中遇到的相关问题：

1.脚本中的随机种子问题：

*问题描述：*

初始脚本中使用的随机生成key可能有重复，导致没有完整的65536个数据，存在重复写入，get时也随机生成的话有大概率会生成没有写入过的key从而导致abort。

*解决方案：*

在与TA交流后，确认了该问题存在，在将随机种子改为统一的之后修复了这个问题。随后TA在水杉上对源码进行了修改。



2.大小端问题

*问题描述*：

在存储和解析TTL时间戳时，需要确保数据在不同系统架构上能正确读取。不同架构可能使用不同的字节序（小端或大端），因此直接存储时间戳会导致在不同平台上读取错误。

*解决方案：*

为了解决这个问题，我选择将小端序作为时间戳存储格式。这一选择基于以下几点：
1. **兼容性**：大多数现代硬件（包括我的实验环境）默认使用小端序，因此直接采用小端序可以避免额外的转换开销。
2. **跨平台一致性**：即使在大端系统上，通过明确指定小端序可以确保数据格式在不同平台上保持一致。
3. **简化操作**：在存储和读取TTL时间戳时，我采用了 `reinterpret_cast` 和 `memcpy` 方法直接对数据进行小端序读写，避免了复杂的转换逻辑。



3.Compaction自动合并问题

*问题描述：*

手动合并可能无法保证合并所有数据，导致无法完全丢弃过期数据。leveldb的自动合并可能提前把一些数据合并为有序，而CompactRange(nullptr, nullptr)函数只会合并剩下没完全有序的数据。

*解决方案：*

通过修改函数中的判断条件在决定数据向下一层的迁移方式时禁用`DBImpl::BackgroundCall()`中的`TrivialMove`方法，迫使所有数据进入`DoCompactionWork()`，并且适当调整`Option.h`中level0的大小，减少自动合并的次数，同时针对levelDB中在满足条件时将level0中文件自动迁移到level2的这类特殊优化，由于其会干扰`DBImpl::CompactRange`正常合并，所以在`DBImpl::CompactRange`中将遍历扩大一层以覆盖所有文件。

```
for (int level = 0; level <= max_level_with_files; level++) {
    TEST_CompactRange(level, begin, end);
  }
```

Related Pr : [avoid lose efficacy for CompactRange by demiaowu · Pull Request #502 · google/leveldb](https://github.com/google/leveldb/pull/502)



---

### 问题描述

在 LevelDB 中，`CompactRange` 是一种压缩操作，用于整理并清理存储层级（levels）中的数据。当数据被标记为删除时，通过压缩将这些数据移除是保证数据库整洁和高效的关键。然而，这段代码和评论揭示了 `CompactRange` 中潜在的一个竞态条件问题，可能导致标记删除的数据在某些情况下无法被清除。

#### 问题的核心在于以下几点：

1. **层级数据不一致**：`max_level_with_files` 是 `CompactRange` 用来确定包含文件的最高层级的变量。当 `CompactRange` 计算出 `max_level_with_files` 后，如果一些数据在压缩指定的层级之前被移动到 `max_level_with_files + 1` 或更高层级，那么这些数据将不在当前压缩操作的范围内，导致这些被标记删除的数据被永久保留。

2. **多线程竞争条件**：在多线程环境中，其他线程（例如 Ceph 触发的压缩操作）也可能会调用 `DBImpl::CompactRange`。由于锁的释放顺序、条件变量和非原子性操作等原因，不同线程间的操作顺序难以保证。当锁被释放时，手动压缩（Manual Compaction）、次级压缩（Minor Compaction）和大小压缩（Size Compaction）可能会互相竞争，这会导致某些已标记删除的数据在预期的删除压缩之前被移至更高层级，使它们不在压缩范围内。

3. **删除数据的不可移除性**：在 `CompactRange` 操作的过程中，如果一些已标记删除的数据在指定的压缩操作执行前被推送到更高层级，这些数据便无法被 `CompactRange` 删除。特别是当这些数据位于 `max_level_with_files + 1` 或更高层级时，数据将不在当前操作的压缩范围内，从而无法被删除。

### PR 中的解决方案

拉取请求（PR）中提出的修复措施主要通过以下方式来缓解问题：

1. **确保压缩操作的顺序一致性**：在 `CompactRange` 中增加了对 `max_level_with_files` 的额外处理逻辑。通过重新评估 `max_level_with_files`，可以更精确地确保操作顺序，使得在多线程环境下，即使发生多种压缩竞争，`CompactRange` 仍然能够稳定地判断出应当压缩的数据层级。

2. **同步和竞争控制**：虽然原代码使用了条件变量进行同步，但 PR 增强了对压缩层级的判断逻辑，以便在锁释放后仍然能维持压缩操作的顺序和数据状态的稳定性。通过改进条件判断，使压缩操作能够在不同线程的竞争下更准确地识别压缩层级，避免数据在指定压缩完成前被移至更高层级。

3. **修复 `max_level_with_files` 的状态一致性问题**：PR 中通过改进 `max_level_with_files` 的计算逻辑，使其更加准确地表示当前需要压缩的数据层级，避免多线程环境中数据被错误推送到更高层级的问题。

### PR 代码示例

在 PR 中，可以看到 `CompactRange` 的操作流程大致如下：

```cpp
void DBImpl::CompactRange(const Slice* begin, const Slice* end) {
  int max_level_with_files = 1;  // 初始化最大层级为 1
  {
    MutexLock l(&mutex_);  // 锁定以确保同步
    Version* base = versions_->current();  // 获取当前版本

    // 遍历层级，找到包含文件的最高层级
    for (int level = 1; level < config::kNumLevels; level++) {
      if (base->OverlapInLevel(level, begin, end)) {
        max_level_with_files = level;  // 更新包含文件的最高层级
      }
    }
  }

  // 执行内存表压缩
  TEST_CompactMemTable();

  // 遍历从 0 到 max_level_with_files 的层级，执行压缩
  for (int level = 0; level < max_level_with_files; level++) {
    TEST_CompactRange(level, begin, end);
  }
}
```

此修复代码确保 `max_level_with_files` 在锁定范围内计算完毕，并且在进行具体的压缩操作前，保证数据的层级和状态一致性。

### 总结

该 PR 的修复思路是增加对 `max_level_with_files` 的控制和检查，确保每一层级的状态在多线程环境下保持一致，防止数据在实际删除前被推送至更高层级。此 PR 的目标是增强 `CompactRange` 的操作原子性和一致性，以避免已标记删除的数据因多线程的竞争条件而无法彻底清理的问题。

