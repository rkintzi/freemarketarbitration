#include <eosiolib/eosio.hpp>
#include <eosiolib/asset.hpp>
#include <eosiolib/print.hpp>
#include <eosiolib/time.hpp>

using namespace eosio;
using namespace std;

time_point current_time_point();

class [[eosio::contract("dep")]] dep : public contract {
  public:
      using contract::contract;
    
      static constexpr uint32_t refund_delay_sec = 30;
      const asset zero = asset(0, symbol("SYS", 4));


      [[eosio::action]]
      void transfer(name from, name to, asset quantity, string memo);

      [[eosio::action]]
      void opendeposit(name buyer, name seller);

      [[eosio::action]]
      void withdraw(name buyer, name seller);

      [[eosio::action]]
      void claim(name buyer, name seller);

      [[eosio::action]]
      void refund(name buyer, name seller);

  private:
      struct [[eosio::table]] dep_rec {
          asset    amount;
          name     seller;
          uint64_t primary_key() const { return seller.value; }
      };

      struct [[eosio::table]] claim_rec {
          name            buyer;
          name            seller;
          asset           amount; 
          time_point_sec  request_time;
          bool            is_withdrawal;
          uint64_t        primary_key() const {return seller.value;}
      };

      typedef eosio::multi_index< "claims"_n, claim_rec > claims;
      typedef eosio::multi_index< "deposits"_n, dep_rec > deposits;
      void sub_balance(name buyer, name seller, asset value);
      void add_balance(name buyer, name seller, asset value);
      void create_claim(name buyer, name seller, bool is_withdrawal);
};

void dep::transfer(name from, name to, asset quantity, string memo) {
    if (from == _self || to != _self) {
        return;
    }
    eosio_assert(quantity.symbol == symbol("SYS", 4), "I think you're looking for another contract");
    eosio_assert(quantity.is_valid(), "Are you trying to corrupt me?");
    eosio_assert(quantity.amount > 0, "When pigs fly");
    deposits db(_self, from.value);
    auto to_acnt = db.find(name(memo).value);
    eosio_assert(to_acnt != db.end(), "Don't send us your money before opening account" );
    add_balance(from, name(memo), quantity);
}

void dep::opendeposit(name buyer, name seller) {
    require_auth(buyer);
    deposits db(_self, buyer.value );
    auto it = db.find(seller.value);
    eosio_assert(it == db.end(), "Depoist already exists");
    add_balance(buyer, seller, zero);
}

void dep::withdraw(name buyer, name seller) {
    require_auth(buyer);
    create_claim(buyer, seller, true);
}

void dep::claim(name buyer, name seller) {
    require_auth(seller);
    create_claim(buyer, seller, false);
}

void dep::create_claim(name buyer, name seller, bool is_withdrawal) {
    deposits dep_db(_self, buyer.value);
    auto deposit = dep_db.find(seller.value);
    eosio_assert(deposit != dep_db.end(), "Deposit not found");
    asset amount = deposit->amount;
    dep_db.erase(deposit);

    claims claims_db(_self, buyer.value);
    auto request = claims_db.find(seller.value);
    if (request == claims_db.end()) {
        claims_db.emplace(is_withdrawal?buyer:seller, [&](auto &claim) {
            claim.buyer = buyer;
            claim.seller = seller;
            claim.request_time = current_time_point();
            claim.amount = amount;
            claim.is_withdrawal = is_withdrawal;
        });
    } else {
        claims_db.modify(request, same_payer, [&](auto &req) {
            req.amount += amount;
            req.request_time = current_time_point();
        });
    }
}

void dep::refund(name buyer, name seller) {
    claims db(_self, buyer.value);
    auto request = db.find(seller.value);
    eosio_assert(request != db.end(), "No claim request found");
    name account;
    if (request->is_withdrawal) {
        require_auth(buyer);
        account = buyer;
    } else {
        require_auth(seller);
        account = seller;
    }
    eosio_assert(request->request_time + seconds(refund_delay_sec) <= current_time_point(),
            "Refund is not available yet" );

    action transfer = action(
        permission_level{get_self() ,"active"_n},
        "eosio.token"_n,
        "transfer"_n,
        std::make_tuple(get_self(), account, request->amount, std::string("Here are your tokens"))
    );
    transfer.send();
    db.erase(request);
}

void dep::add_balance(name buyer, name seller, asset value) {
   deposits db(_self, buyer.value);
   auto dep = db.find(seller.value);
   if(dep == db.end()) {
      db.emplace(buyer, [&]( auto& a ){
        a.seller = seller;
        a.amount = value;
      });
   } else {
      db.modify(dep, same_payer, [&]( auto& a ) {
        a.amount += value;
      });
   }
}

extern "C" {
    void apply(uint64_t receiver, uint64_t code, uint64_t action) {
        auto self = receiver;
        if(code == self && action == name("opendeposit").value) {
              execute_action(name(receiver), name(code), &dep::opendeposit); 
        } else if(code == self && action == name("withdraw").value) {
              execute_action(name(receiver), name(code), &dep::withdraw); 
        } else if(code == self && action == name("claim").value) {
              execute_action(name(receiver), name(code), &dep::claim); 
        } else if(code == self && action == name("refund").value) {
              execute_action(name(receiver), name(code), &dep::refund); 
        } else if(code == name("eosio.token").value && action == name("transfer").value) {
              execute_action(name(receiver), name(code), &dep::transfer); 
        } else{
            eosio_assert(false, (string("Ooops - action not configured: ")+ name(action).to_string()).c_str());
        }
    }
}

time_point current_time_point() {
   const static time_point ct{ microseconds{ static_cast<int64_t>( current_time() ) } };
   return ct;
}
