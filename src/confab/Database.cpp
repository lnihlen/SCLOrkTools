#include "Database.hpp"

#include "common/Version.hpp"

// Flatbuffer includes generated by calling flatc on the Flatbuffer schema (.fbs) files.
#include "schemas/FlatAsset_generated.h"
#include "schemas/FlatAssetData_generated.h"
#include "schemas/FlatConfig_generated.h"


#include <glog/logging.h>
#include <leveldb/cache.h>

#include <cstring>
#include <inttypes.h>
#include <string>
#include <utility>

namespace {
    const char* kConfigKey = "confab-db-config";
}  // namespace

namespace Confab {

Database::Database(leveldb::DB* database) :
    m_database(nullptr) {
}

Database::~Database() {
    close();
}

bool Database::open(const char* path, bool createNew, int cacheSize) {
    leveldb::Options options;
    options.create_if_missing = createNew;
    options.error_if_exists = createNew;
    if (cacheSize > 0) {
        options.block_cache = leveldb::NewLRUCache(cacheSize);
    }

    leveldb::Status status = leveldb::DB::Open(options, path, &m_database);
    if (!status.ok()) {
        LOG(ERROR) << "Failure opening or creating database at '" << path << "'. LevelDB status: " << status.ToString();
        return false;
    } else {
        LOG(INFO) << "Opened database file at '" << path << "'.";
    }

    return true;
}

ConfigPtr Database::findConfig() {
    CHECK(m_database) << "Database should already be open.";

    std::unique_ptr<leveldb::Iterator> iterator(m_database->NewIterator(leveldb::ReadOptions()));
    iterator->Seek(kConfigKey);

    if (!iterator->Valid() || iterator->key() != std::string(kConfigKey)) {
        return ConfigPtr(nullptr);
    }

    auto value = iterator->value();
    auto verifier = flatbuffers::Verifier(reinterpret_cast<const uint8_t*>(value.data()), value.size());
    auto ok = Data::VerifyFlatConfigBuffer(verifier);

    if (!ok) {
        LOG(ERROR) << "Flatbuffer validation failed for database config key.";
        return ConfigPtr(nullptr);
    }

    auto config = Data::GetFlatConfig(value.data());
    return ConfigPtr(config, std::move(iterator));
}

bool Database::writeConfig(const Common::Version& version) {
    flatbuffers::FlatBufferBuilder builder;
    Confab::Data::FlatConfigBuilder configBuilder(builder);
    configBuilder.add_versionMajor(version.major());
    configBuilder.add_versionMinor(version.minor());
    configBuilder.add_versionPatch(version.patch());
    auto config = configBuilder.Finish();
    builder.Finish(config, Data::FlatConfigIdentifier());
    auto status = m_database->Put(leveldb::WriteOptions(), kConfigKey, leveldb::Slice(reinterpret_cast<const char*>(
        builder.GetBufferPointer()), builder.GetSize()));

    return status.ok();
}

AssetPtr Database::findAsset(uint64_t key) {
    CHECK(m_database) << "Database should already be open.";

    auto assetKey = makeAssetKey(key);
    auto keySlice = leveldb::Slice(reinterpret_cast<const char*>(assetKey.data()), sizeof(AssetKey));

    std::unique_ptr<leveldb::Iterator> iterator(m_database->NewIterator(leveldb::ReadOptions()));
    iterator->Seek(keySlice);

    if (!iterator->Valid()) {
        LOG(INFO) << "Invalid iterator on asset seek for key: " << keyToString(key);
        return AssetPtr(nullptr);
    }

    if (iterator->key().size() != sizeof(AssetKey) ||
        std::memcmp(keySlice.data(), iterator->key().data(), sizeof(AssetKey)) != 0) {
        LOG(INFO) << "Asset " << keyToString(key) << " not found in database.";
        return AssetPtr(nullptr);
    }

    auto asset = Data::GetFlatAsset(iterator->value().data());
    return AssetPtr(asset, std::move(iterator));
}

DataPtr Database::findAssetData(uint64_t key) {
    CHECK(m_database) << "Database should already be open.";

    auto dataKey = makeDataKey(key);
    auto keySlice = leveldb::Slice(reinterpret_cast<const char*>(dataKey.data()), sizeof(AssetKey));

    std::unique_ptr<leveldb::Iterator> iterator(m_database->NewIterator(leveldb::ReadOptions()));
    iterator->Seek(keySlice);

    if (!iterator->Valid()) {
        LOG(INFO) << "Invalid iterator on asset data seek for key: " << keyToString(key);
        return DataPtr(nullptr);
    }

    if (iterator->key().size() != sizeof(AssetKey) ||
        std::memcmp(keySlice.data(), iterator->key().data(), sizeof(AssetKey)) != 0) {
        LOG(INFO) << "Asset data " << keyToString(key) << " not found in database.";
        return DataPtr(nullptr);
    }

    auto data = Data::GetFlatAssetData(iterator->value().data());
    return DataPtr(data, std::move(iterator));
}

void Database::close() {
    delete m_database;
    m_database = nullptr;
}

// static
std::string Database::keyToString(uint64_t key) {
    std::array<char, 17> buf;
    snprintf(buf.data(), 17, "%" PRIx64, key);
    return std::string(buf.data());
}

// static
Database::AssetKey Database::makeAssetKey(uint64_t key) {
    AssetKey assetKey;
    assetKey[0] = KeyPrefix::kAsset;
    std::memcpy(assetKey.data() + 1, reinterpret_cast<uint8_t*>(&key), sizeof(AssetKey) - 1);
    return assetKey;
}

// static
Database::AssetKey Database::makeDataKey(uint64_t key) {
    AssetKey dataKey;
    dataKey[0] = KeyPrefix::kData;
    std::memcpy(dataKey.data() + 1, reinterpret_cast<uint8_t*>(&key), sizeof(AssetKey));
    return dataKey;
}

}  // namespace Confab

