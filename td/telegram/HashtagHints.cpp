//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2019
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/HashtagHints.h"

#include "td/telegram/Global.h"
#include "td/telegram/TdDb.h"

#include "td/db/Pmc.h"
#include "td/db/SqliteKeyValueAsync.h"

#include "td/utils/logging.h"
#include "td/utils/tl_helpers.h"

#include <functional>

namespace td {

HashtagHints::HashtagHints(string mode, ActorShared<> parent) : mode_(std::move(mode)), parent_(std::move(parent)) {
}

void HashtagHints::start_up() {
  if (G()->parameters().use_file_db) {  // TODO hashtag hints should not depend on use_file_db
    G()->td_db()->get_sqlite_pmc()->get(get_key(),
                                        PromiseCreator::lambda([actor_id = actor_id(this)](Result<string> res) {
                                          send_closure(actor_id, &HashtagHints::from_db, std::move(res), false);
                                        }));
  }
}

void HashtagHints::hashtag_used(const string &hashtag) {
  if (!sync_with_db_) {
    return;
  }
  hashtag_used_impl(hashtag);
  G()->td_db()->get_sqlite_pmc()->set(get_key(), serialize(keys_to_strings(hints_.search_empty(101).second)),
                                      Promise<>());
}

void HashtagHints::remove_hashtag(string hashtag, Promise<> promise) {
  if (!sync_with_db_) {
    promise.set_value(Unit());
    return;
  }
  if (hashtag[0] == '#') {
    hashtag = hashtag.substr(1);
  }
  auto key = std::hash<std::string>()(hashtag);
  if (hints_.has_key(key)) {
    hints_.remove(key);
    G()->td_db()->get_sqlite_pmc()->set(get_key(), serialize(keys_to_strings(hints_.search_empty(101).second)),
                                        Promise<>());
    promise.set_value(Unit());  // set promise explicitly, because sqlite_pmc waits for too long before setting promise
  } else {
    promise.set_value(Unit());
  }
}

void HashtagHints::query(const string &prefix, int32 limit, Promise<std::vector<string>> promise) {
  if (!sync_with_db_) {
    promise.set_value(std::vector<string>());
    return;
  }

  auto result = prefix.empty() ? hints_.search_empty(limit) : hints_.search(prefix, limit);
  promise.set_value(keys_to_strings(result.second));
}

string HashtagHints::get_key() const {
  return "hashtag_hints#" + mode_;
}

void HashtagHints::hashtag_used_impl(const string &hashtag) {
  // TODO: may be it should be optimized a little
  auto key = std::hash<std::string>()(hashtag);
  hints_.add(key, hashtag);
  hints_.set_rating(key, -++counter_);
}

void HashtagHints::from_db(Result<string> data, bool dummy) {
  sync_with_db_ = true;
  if (data.is_error() || data.ok().empty()) {
    return;
  }
  std::vector<string> hashtags;
  auto status = unserialize(hashtags, data.ok());
  if (status.is_error()) {
    LOG(ERROR) << status;
    return;
  }

  for (auto it = hashtags.rbegin(); it != hashtags.rend(); ++it) {
    hashtag_used_impl(*it);
  }
}

std::vector<string> HashtagHints::keys_to_strings(const std::vector<int64> &keys) {
  std::vector<string> result;
  result.reserve(keys.size());
  for (auto &it : keys) {
    result.push_back(hints_.key_to_string(it));
  }
  return result;
}
}  // namespace td
