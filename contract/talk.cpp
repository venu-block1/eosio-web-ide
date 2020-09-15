#include <eosio/eosio.hpp>

// Message table
struct [[eosio::table("message"), eosio::contract("talk")]] message {
    uint64_t    id       = {}; // Non-0
    uint64_t    reply_to = {}; // Non-0 if this is a reply
    eosio::name user     = {};
    std::string content  = {};
    int         like     = {};

    uint64_t primary_key() const { return id; }
    uint64_t get_reply_to() const { return reply_to; }
};

using message_table = eosio::multi_index<
    "message"_n, message, eosio::indexed_by<"by.reply.to"_n, eosio::const_mem_fun<message, uint64_t, &message::get_reply_to>>>;

namespace {
    uint128_t likes_table_sec_key(eosio::name user, uint64_t msg_id)
    { 
	eosio::name::raw raw_user = user;
	uint128_t key = (uint128_t) raw_user << 64 | msg_id;
	return key;
    }
} // anonymous

// Likes table
struct [[eosio::table("likes"), eosio::contract("talk")]] like {
    uint64_t    id           = {}; // Non-0
    uint64_t    msg_id       = {}; // Non-0
    eosio::name user         = {};
    int         like         = {};

    uint64_t primary_key() const { return id; }
    uint128_t get_name_msg_id() const { return likes_table_sec_key(user, msg_id); }
};

using likes_table = eosio::multi_index<
    "likes"_n, like, eosio::indexed_by<"name.msg.id"_n, eosio::const_mem_fun<like, uint128_t, &like::get_name_msg_id>>>;

// The contract
class talk : eosio::contract {
  public:
    // Use contract's constructor
    using contract::contract;

    // Post a message
    [[eosio::action]] void post(uint64_t id, uint64_t reply_to, eosio::name user, const std::string& content) {
        message_table table{get_self(), 0};

        // Check user
        require_auth(user);

        // Check reply_to exists
        if (reply_to)
            table.get(reply_to);

        // Create an ID if user didn't specify one
        eosio::check(id < 1'000'000'000ull, "user-specified id is too big");
        if (!id)
            id = std::max(table.available_primary_key(), 1'000'000'000ull);

        // Record the message
        table.emplace(get_self(), [&](auto& message) {
            message.id       = id;
            message.reply_to = reply_to;
            message.user     = user;
            message.content  = content;
            message.like     = 0;
        });
    }

    // Like (or dislike) a message
    [[eosio::action]] void like(uint64_t id, uint64_t msg_id, eosio::name user, bool like) {
        message_table msg_table{get_self(), 0};
        likes_table likes_table{get_self(), 0};

        // Check user
        require_auth(user);

        // Check msg_id exists
        if (msg_id)
            msg_table.get(msg_id);

	// Check if user already liked msg_id
        auto sec_index = likes_table.get_index<"name.msg.id"_n>();
        auto itr = sec_index.find( likes_table_sec_key(user, msg_id) );
        eosio::check(itr == sec_index.end(), "User already liked  this message");

        // Create an ID if user didn't specify one
        eosio::check(id < 1'000'000'000ull, "user-specified id is too big");
        if (!id)
            id = std::max(likes_table.available_primary_key(), 1'000'000'000ull);

        // Record the like
        likes_table.emplace(get_self(), [&](auto& tbl_entry) {
            tbl_entry.id           = id;
            tbl_entry.msg_id       = msg_id;
            tbl_entry.user         = user;
            tbl_entry.like         = like ? 1 : -1;
        });

        // Update msg_id with like
        msg_table.modify(msg_table.get(msg_id), user, [&](auto& message) {
            message.like += like ? 1 : -1;
        });
    }
};
