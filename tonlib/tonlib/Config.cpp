/*
    This file is part of TON Blockchain Library.

    TON Blockchain Library is free software: you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    TON Blockchain Library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with TON Blockchain Library.  If not, see <http://www.gnu.org/licenses/>.

    Copyright 2017-2020 Telegram Systems LLP
*/
#include "Config.h"
#include "adnl/adnl-node-id.hpp"
#include "td/utils/JsonBuilder.h"
#include "auto/tl/ton_api_json.h"

namespace tonlib {
td::Result<ton::BlockIdExt> parse_block_id_ext(td::JsonObject &obj) {
  ton::WorkchainId zero_workchain_id;
  {
    TRY_RESULT(wc, td::get_json_object_int_field(obj, "workchain"));
    zero_workchain_id = wc;
  }
  ton::ShardId zero_shard_id;  // uint64
  {
    TRY_RESULT(shard_id, td::get_json_object_long_field(obj, "shard"));
    zero_shard_id = static_cast<ton::ShardId>(shard_id);
  }
  ton::BlockSeqno zero_seqno;
  {
    TRY_RESULT(seqno, td::get_json_object_int_field(obj, "seqno"));
    zero_seqno = seqno;
  }

  ton::RootHash zero_root_hash;
  {
    TRY_RESULT(hash_b64, td::get_json_object_string_field(obj, "root_hash"));
    TRY_RESULT(hash, td::base64_decode(hash_b64));
    if (hash.size() * 8 != ton::RootHash::size()) {
      return td::Status::Error("Invalid config (8)");
    }
    zero_root_hash = ton::RootHash(td::ConstBitPtr(td::Slice(hash).ubegin()));
  }
  ton::FileHash zero_file_hash;
  {
    TRY_RESULT(hash_b64, td::get_json_object_string_field(obj, "file_hash"));
    TRY_RESULT(hash, td::base64_decode(hash_b64));
    if (hash.size() * 8 != ton::FileHash::size()) {
      return td::Status::Error("Invalid config (9)");
    }
    zero_file_hash = ton::RootHash(td::ConstBitPtr(td::Slice(hash).ubegin()));
  }

  return ton::BlockIdExt(zero_workchain_id, zero_shard_id, zero_seqno, std::move(zero_root_hash),
                         std::move(zero_file_hash));
}
td::Result<Config> Config::parse(std::string str) {
  TRY_RESULT(json, td::json_decode(str));
  if (json.type() != td::JsonValue::Type::Object) {
    return td::Status::Error("Invalid config (1)");
  }

  Config res;
  ton::ton_api::liteclient_config_global conf;
  TRY_STATUS(ton::ton_api::from_json(conf, json.get_object()));
  TRY_RESULT_ASSIGN(res.lite_servers, liteclient::LiteServerConfig::parse_global_config(conf));

  TRY_RESULT(validator_obj,
             td::get_json_object_field(json.get_object(), "validator", td::JsonValue::Type::Object, false));
  auto &validator = validator_obj.get_object();
  TRY_RESULT(validator_type, td::get_json_object_string_field(validator, "@type", false));
  if (validator_type != "validator.config.global") {
    return td::Status::Error("Invalid config (7)");
  }
  TRY_RESULT(zero_state_obj, td::get_json_object_field(validator, "zero_state", td::JsonValue::Type::Object, false));
  TRY_RESULT(zero_state_id, parse_block_id_ext(zero_state_obj.get_object()));
  res.zero_state_id = zero_state_id;
  auto r_init_block_obj = td::get_json_object_field(validator, "init_block", td::JsonValue::Type::Object, false);
  if (r_init_block_obj.is_ok()) {
    TRY_RESULT(init_block_id, parse_block_id_ext(r_init_block_obj.move_as_ok().get_object()));
    res.init_block_id = init_block_id;
  }

  auto r_hardforks = td::get_json_object_field(validator, "hardforks", td::JsonValue::Type::Array, false);
  if (r_hardforks.is_ok()) {
    auto hardforks_obj = r_hardforks.move_as_ok();
    auto &hardforks = hardforks_obj.get_array();
    for (auto &fork : hardforks) {
      if (fork.type() != td::JsonValue::Type::Object) {
        return td::Status::Error("Invalid config (8)");
      }
      TRY_RESULT(fork_block, parse_block_id_ext(fork.get_object()));
      res.hardforks.push_back(std::move(fork_block));
    }
  }

  for (auto hardfork : res.hardforks) {
    if (!res.init_block_id.is_valid() || hardfork.seqno() > res.init_block_id.seqno()) {
      LOG(INFO) << "Replace init_block with hardfork: " << res.init_block_id.to_str() << " -> " << hardfork.to_str();
      res.init_block_id = hardfork;
    }
  }

  return res;
}
}  // namespace tonlib
