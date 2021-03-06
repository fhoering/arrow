// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#include "arrow/dataset/partition.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "arrow/dataset/filter.h"
#include "arrow/dataset/scanner.h"
#include "arrow/filesystem/filesystem.h"
#include "arrow/filesystem/path_util.h"
#include "arrow/scalar.h"
#include "arrow/util/iterator.h"

namespace arrow {
namespace dataset {

Result<ExpressionPtr> ConvertPartitionKeys(const std::vector<UnconvertedKey>& keys,
                                           const Schema& schema) {
  ExpressionVector subexpressions;

  for (const auto& key : keys) {
    auto field = schema.GetFieldByName(key.name);
    if (field == nullptr) {
      continue;
    }

    std::shared_ptr<Scalar> converted;
    RETURN_NOT_OK(Scalar::Parse(field->type(), key.value, &converted));
    subexpressions.push_back(equal(field_ref(field->name()), scalar(converted)));
  }

  return and_(subexpressions);
}

Result<ExpressionPtr> ConstantPartitionScheme::Parse(const std::string& path) const {
  return expression_;
}

Result<ExpressionPtr> SchemaPartitionScheme::Parse(const std::string& path) const {
  auto segments = fs::internal::SplitAbstractPath(path);
  auto min = std::min(static_cast<int>(segments.size()), schema_->num_fields());
  std::vector<UnconvertedKey> keys(min);
  for (int i = 0; i < min; i++) {
    keys[i].name = schema_->field(i)->name();
    keys[i].value = std::move(segments[i]);
  }

  return ConvertPartitionKeys(keys, *schema_);
}

std::vector<UnconvertedKey> HivePartitionScheme::GetUnconvertedKeys(
    const std::string& path) const {
  auto segments = fs::internal::SplitAbstractPath(path);

  std::vector<UnconvertedKey> keys;
  for (const auto& segment : segments) {
    std::smatch matches;
    static std::regex hive_style("^([^=]+)=(.*)$");
    if (std::regex_match(segment, matches, hive_style) && matches.size() == 3) {
      keys.push_back({matches[1].str(), matches[2].str()});
    }
  }
  return keys;
}

Result<ExpressionPtr> HivePartitionScheme::Parse(const std::string& path) const {
  return ConvertPartitionKeys(GetUnconvertedKeys(path), *schema_);
}

Result<PathPartitions> ApplyPartitionScheme(const PartitionScheme& scheme,
                                            std::vector<fs::FileStats> files,
                                            PathPartitions* out) {
  return ApplyPartitionScheme(scheme, "", std::move(files));
}

Result<PathPartitions> ApplyPartitionScheme(const PartitionScheme& scheme,
                                            const std::string& base_dir,
                                            std::vector<fs::FileStats> files) {
  PathPartitions partitions;

  for (const auto& file : files) {
    if (file.path().substr(0, base_dir.size()) != base_dir) continue;
    auto path = file.path().substr(base_dir.size());

    ARROW_ASSIGN_OR_RAISE(auto partition, scheme.Parse(path));
    partitions.emplace(std::move(path), std::move(partition));
  }

  return partitions;
}

}  // namespace dataset
}  // namespace arrow
