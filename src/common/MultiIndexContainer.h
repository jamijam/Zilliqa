/**
* Copyright (c) 2018 Zilliqa 
* This source code is being disclosed to you solely for the purpose of your participation in 
* testing Zilliqa. You may view, compile and run the code for that purpose and pursuant to 
* the protocols and algorithms that are programmed into, and intended by, the code. You may 
* not do anything else with the code without express permission from Zilliqa Research Pte. Ltd., 
* including modifying or publishing the code (or any part of it), and developing or forming 
* another public or private blockchain network. This source code is provided ‘as is’ and no 
* warranties are given as to title or non-infringement, merchantability or fitness for purpose 
* and, to the extent permitted by law, all liability for your use of the code is disclaimed. 
* Some programs in this code are governed by the GNU General Public License v3.0 (available at 
* https://www.gnu.org/licenses/gpl-3.0.en.html) (‘GPLv3’). The programs that are governed by 
* GPLv3.0 are those programs that are located in the folders src/depends and tests/depends 
* and which include a reference to GPLv3 in their program files.
**/

#include <boost/multi_index/composite_key.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/key_extractors.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index_container.hpp>

#include "libData/AccountData/Transaction.h"

enum MULTI_INDEX_KEY : unsigned int
{
    GAS_PRICE = 0,
    TXN_ID,
    ADDR_NONCE
};

typedef boost::multi_index::ordered_non_unique<
    boost::multi_index::const_mem_fun<Transaction,
                                      const boost::multiprecision::uint256_t&,
                                      &Transaction::GetGasPrice>>
    ordered_non_unique_gas_key;

typedef boost::multi_index::hashed_unique<boost::multi_index::const_mem_fun<
    Transaction, const TxnHash&, &Transaction::GetTranID>>
    hashed_unique_txnid_key;

typedef boost::multi_index::hashed_unique<boost::multi_index::composite_key<
    Transaction,
    boost::multi_index::const_mem_fun<Transaction, Address,
                                      &Transaction::GetSenderAddr>,
    boost::multi_index::const_mem_fun<Transaction,
                                      const boost::multiprecision::uint256_t&,
                                      &Transaction::GetNonce>>>
    hashed_unique_comp_addr_nonce_key;

namespace boost
{
    namespace multiprecision
    {
        static std::size_t hash_value(const uint256_t& value)
        {
            std::size_t seed = 0;
            boost::hash_combine(seed, value);
            return seed;
        }
    }
}

typedef boost::multi_index::multi_index_container<
    Transaction,
    boost::multi_index::indexed_by<ordered_non_unique_gas_key,
                                   hashed_unique_txnid_key,
                                   hashed_unique_comp_addr_nonce_key>>
    gas_txnid_comp_txns;