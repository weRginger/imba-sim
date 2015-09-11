//
// C++ Interface: hybrid-dynamic-withpcr
//
// Description: Cache policy for hybrid main memory using NVRAM and DRAM.
//It can self-adjust to workload based on DARC and has a concept of PCR (Page Copy on lru list for Read) to better utilize NVRAM space for dirty pages
//
// Author: Ziqi Fan, (C) 2014
//
// Copyright: See COPYING file that comes with this distribution
//

#ifndef HybridDynamicWithPCR_H
#define HybridDynamicWithPCR_H

#include <map>
#include <list>
#include "cassert"
#include "iostream"
#include "iomanip"
#include "bitset"
#include "global.h"
#include "baseCache.h"

using namespace std;

///the real page stored at DRAM, did not consider PCR pages
extern unsigned int realPageAtDRAM;

///extern int totalEvictedCleanPages;

extern int totalPageWriteToStorage;
///ziqi test the pingpong Phenomenon of hybrid-dynamic-withpcr
extern int totalPageWriteToStorageWithPingpong;

extern int writeHitOnDirty;

extern int writeHitOnClean;

extern int dirtyPageInCache;

extern double priceDvsN;

extern double moneyAllo4D;

// Class providing fixed-size (by number of records)
// LRU-replacement cache of a function with signature
// V f(K)
template <typename K, typename V>
class HybridDynamicWithPCR : public TestCache<K, V>
{
public:
    // Key access history, most recent at back
    typedef list<K> key_tracker_type;
    // Key to value and key history iterator
    typedef map
    < K, pair<V, typename key_tracker_type::iterator> > 	key_to_value_type;
    /// List of migrated page from NVRAM to DRAM
    typedef list<int> migrated_list_type;

    // Constuctor specifies the cached function and
    // the maximum number of records to be stored.
    HybridDynamicWithPCR(
        V(*f)(const K & , V),
        size_t c,
        unsigned levelMinus
    ) : _fn(f) , _capacity(c), levelMinusMinus(levelMinus)  {
        //ARH: Commented for single level cache implementation
        //assert ( _capacity!=0 );
    }
    // Obtain value of the cached function for k

    uint32_t access(const K &k  , V &value, uint32_t status) {
        size_t DRAM_capacity = (size_t)_capacity * moneyAllo4D;
        int NVM_capacity = (int)_capacity * (1 - moneyAllo4D) * priceDvsN;

        ///Delete this if priceDvsN is not equal to 1.0
        if(DRAM_capacity + NVM_capacity < _capacity) {
            NVM_capacity = _capacity - DRAM_capacity;
        }
        PRINTV(logfile << endl;);
        ///ziqi: p denotes the length of t1 and (_capacity - p) denotes the lenght of t2
        static int p=0;

        assert((realPageAtDRAM + t1b.size() + t2.size()) <= _capacity);
        assert((realPageAtDRAM + t1b.size() + t2.size() + b1.size() + b2.size()) <= 2*_capacity);
        assert(t2.size() <= (unsigned)NVM_capacity);
        assert(t1b.size() <= (unsigned)NVM_capacity);
        assert(realPageAtDRAM <= DRAM_capacity);
        assert((realPageAtDRAM + t1b.size() + b1.size()) <= _capacity);
        assert((t2.size() + b2.size()) <= 2*_capacity);
        assert(_capacity != 0);
        PRINTV(logfile << "Access key: " << k << endl;);

        ///Keep track of the number of dirty page in cache
        ///in order to know how many pages did not have the chance to be flushed
        dirtyPageInCache = int(t2.size());
        PRINTV(logfile << "Dirty Page Number In Cache: " << dirtyPageInCache << endl;);

        // Attempt to find existing record
        const typename key_to_value_type::iterator it_t1a	= t1a.find(k);
        const typename key_to_value_type::iterator it_t1b	= t1b.find(k);
        const typename key_to_value_type::iterator it_t2	= t2.find(k);
        const typename key_to_value_type::iterator it_b1	= b1.find(k);
        const typename key_to_value_type::iterator it_b2	= b2.find(k);
        //const typename key_to_value_type::iterator itNew	= _key_to_value.find(k);
        ///*************************************************************************************************
        ///ziqi: HYBRID-Dynamic-WithPCR Case I: x hit in t2, then move x to MRU of t2
        if(it_t2 != t2.end()) {
            ///ziqi: if it is a write request
            if(status & WRITE) {
                PRINTV(logfile << "Case I write hit on t2 with key: " << k <<endl;);
                t2.erase(it_t2);
                t2_key.remove(k);
                assert(t2.size() < _capacity);
                const V v = _fn(k, value);
                // Record k as most-recently-used key
                typename key_tracker_type::iterator itNew = t2_key.insert(t2_key.end(), k);
                // Create the key-value entry,
                // linked to the usage record.
                assert(t1b.size() + t2.size() < (unsigned)NVM_capacity);
                t2.insert(make_pair(k, make_pair(v, itNew)));
                PRINTV(logfile << "Case I write hit insert key to t2: " << k << "** t1a size: "<< t1a.size()<<", realPageAtDRAM size: "<<realPageAtDRAM<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
            }
            ///a read request
            else {
                PRINTV(logfile << "Case I read hit on t2 with key: " << k <<endl;);
                ///check whether page k in t2 has a PCR in t1a, if it has, just move the PCR to MRU of t1_key
                if(it_t1a != t1a.end()) {
                    ///make sure it is a PCR instead of a real page
                    assert(it_t1a->second.first.getReq().flags & PCR);
                    PRINTV(logfile << "Have PCR before, migrate from current to MRU of t1_key "<< endl;);
                    t1a.erase(it_t1a);
                    t1_key.remove(k);
                    ///maintain the page's PCR to be 1.
                    value.updateFlags(value.getReq().flags | PCR);
                    const V v = _fn(k, value);
                    // Record k as most-recently-used key
                    typename key_tracker_type::iterator itNew = t1_key.insert(t1_key.end(), k);
                    // Create the key-value entry,
                    // linked to the usage record.
                    t1a.insert(make_pair(k, make_pair(v, itNew)));
                    PRINTV(logfile << "Case I read hit move PCR to MRU of t1_key: " << k << "** t1a size: "<< t1a.size()<<", realPageAtDRAM size: "<<realPageAtDRAM<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                }
                ///if the hitted page did not have a PCR at lru list for read, create one and insert it to MRU of t1_key
                else {
                    PRINTV(logfile << "no PCR before, create one " <<endl;);
                    ///Set the page's PCR to be 1.
                    PRINTV(logfile << "Key status before change: " << bitset<14>(value.getReq().flags) << endl;);
                    value.updateFlags(value.getReq().flags | PCR);
                    PRINTV(logfile << "Key status after change: " << bitset<14>(value.getReq().flags) << endl;);

                    const V v = _fn(k, value);
                    // Record k as most-recently-used key

                    typename key_tracker_type::iterator itNew = t1_key.insert(t1_key.end(), k);
                    // Create the key-value entry,
                    // linked to the usage record.
                    t1a.insert(make_pair(k, make_pair(v, itNew)));
                    PRINTV(logfile << "Case I read hit create PCR and insert to MRU of t1_key: " << k << "** t1a size: "<< t1a.size()<<", realPageAtDRAM size: "<<realPageAtDRAM<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                }
            }
            return (status | PAGEHIT | BLKHIT);
        }
        ///*************************************************************************************************
        ///ziqi: HYBRID-Dynamic Case II: x hit in t1, then move x to t1 if it's a read, or to t2 if it's a write
        else if((it_t1a != t1a.end()) || (it_t1b != t1b.end())) {
            assert(!((it_t1a != t1a.end()) && (it_t1b != t1b.end())));
            ///ziqi: if it is a write request
            if(status & WRITE) {
                if(it_t1a != t1a.end()) {
                    PRINTV(logfile << "Case II hit on t1a: " << k << endl;);
                    ///mark the hited page to be a PCR and the real page number at DRAM decrease by 1
                    PRINTV(logfile << "Change the hit page on t1a from a real page to a PCR " <<endl;);
                    ///make sure the hit page in t1a is a real page
                    assert( !(it_t1a->second.first.getReq().flags & PCR) );
                    PRINTV(logfile << "Key status before change: " << bitset<14>(it_t1a->second.first.getReq().flags) << endl;);
                    it_t1a->second.first.updateFlags(it_t1a->second.first.getReq().flags | PCR);
                    PRINTV(logfile << "Key status after change: " << bitset<14>(it_t1a->second.first.getReq().flags) << endl;);
                    ///decrease one number of the real pages at DRAM
                    realPageAtDRAM--;
                    ///NVRAM is not full
                    if(t2.size()+t1b.size() < (unsigned)NVM_capacity) {
                        PRINTV(logfile << "Case II Write hit on t1a, and NVRAM is not full: " << k << endl;);
                        ///insert x to MRU of t2
                        const V v = _fn(k, value);
                        // Record k as most-recently-used key
                        typename key_tracker_type::iterator itNew = t2_key.insert(t2_key.end(), k);
                        // Create the key-value entry,
                        // linked to the usage record.
                        assert(t1b.size() + t2.size() < (unsigned)NVM_capacity);
                        t2.insert(make_pair(k, make_pair(v, itNew)));
                        PRINTV(logfile << "Case II insert dirty key to t2: " << k << "** t1a size: "<< t1a.size()<<", realPageAtDRAM size: "<<realPageAtDRAM<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                    }
                    ///NVRAM is full
                    if(t2.size()+t1b.size() == (unsigned)NVM_capacity) {
                        ///NVRAM is filled with dirty pages
                        if(t2.size() == (unsigned)NVM_capacity) {
                            PRINTV(logfile << "Case II Write hit on t1a, and NVRAM is full, t2.size() == NVRAM: " << k << endl;);
                            ///select the LRU page of t2
                            typename key_tracker_type::iterator itLRU = t2_key.begin();
                            assert(itLRU != t2_key.end());
                            typename key_to_value_type::iterator it = t2.find(*itLRU);
                            assert(it != t2.end());
                            ///check whether the LRU page of t2 has a PCR at t1a
                            typename key_to_value_type::iterator itPCRatDRAM = t1a.find(*itLRU);
                            ///if the LRU page of t2 has a PCR at t1a,
                            ///change the PCR to a real page
                            if(itPCRatDRAM != t1a.end()) {
                                ///make sure it is a PCR instead of a real page
                                assert(itPCRatDRAM->second.first.getReq().flags & PCR);
                                PRINTV(logfile << "Change the PCR of LRU page of t2 from a PCR to a real page " <<endl;);
                                assert(realPageAtDRAM < DRAM_capacity);
                                PRINTV(logfile << "Key status before change: " << bitset<14>(itPCRatDRAM->second.first.getReq().flags) << endl;);
                                itPCRatDRAM->second.first.updateFlags(itPCRatDRAM->second.first.getReq().flags & ~PCR);
                                PRINTV(logfile << "Key status after change: " << bitset<14>(itPCRatDRAM->second.first.getReq().flags) << endl;);
                                ///increase one number of the real pages at DRAM
                                realPageAtDRAM++;
                            }
                            ///if the LRU page of t2 has no PCR at t1a
                            ///insert the LRU page for t2 to t1a, since t1a has at least one page space left
                            ///Note that we insert the page from t2 to the LRU position of t1, it is NOT MRU position
                            else {
                                typename key_tracker_type::iterator itNewTmp = t1_key.insert(t1_key.begin(), *itLRU);
                                // Create the key-value entry,
                                // linked to the usage record.
                                const V v_tmp = _fn(*itLRU, it->second.first);
                                assert(realPageAtDRAM < DRAM_capacity);
                                t1a.insert(make_pair(*itLRU, make_pair(v_tmp, itNewTmp)));
                                realPageAtDRAM++;
                                PRINTV(logfile << "Case II insert the LRU page of t2 to LRU position of t1 on t1a: " << *itLRU << "** t1a size: "<< t1a.size()<<", realPageAtDRAM size: "<<realPageAtDRAM<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                            }

                            ///flush back and evcit the LRU page of t2
                            t2.erase(it);
                            t2_key.remove(*itLRU);
                            PRINTV(logfile << "Case II (NVM is filled with dirty pages) evicting t2 and flushing back " << *itLRU << "** t1a size: "<< t1a.size()<<", realPageAtDRAM size: "<<realPageAtDRAM<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                            totalPageWriteToStorage++;

                            ///check whether LRU page of t2 exists in migrated list
                            typename migrated_list_type::iterator itMigratedList = find(migrated_list.begin(), migrated_list.end(), *itLRU);
                            if(itMigratedList != migrated_list.end()) {
                                totalPageWriteToStorageWithPingpong++;
                                PRINTV(logfile << "migrated_list contains key: " << *itLRU << "** totalPageWriteToStorageWithPingpong size: "<< totalPageWriteToStorageWithPingpong <<endl;);
                            }
                            ///insert the LRU page of t2 to migrated list
                            if(itMigratedList == migrated_list.end()) {
                                migrated_list.insert(migrated_list.begin(), *itLRU);
                                PRINTV(logfile << "migrated_list added key: " << *itLRU << "** migrated_list size: "<< migrated_list.size() <<endl;);
                            }
                        }
                        ///NVRAM is full but is not filled with dirty pages
                        else { ///t2.size() < (unsigned)NVM_capacity
                            PRINTV(logfile << "Case II Write hit on t1a, and NVRAM is full, t2.size() < NVRAM: " << k << endl;);
                            ///migrate a clean page from t1b to t1a
                            typename key_to_value_type::iterator it_t1b_tmp = t1b.begin();
                            assert(it_t1b_tmp != t1b.end());
                            ///make sure the random picked page in t1b is a real page
                            assert( !(it_t1b_tmp->second.first.getReq().flags & PCR) );
                            typename key_tracker_type::iterator it_t1b_key_tmp = t1_key.begin();
                            const V v_tmp = _fn(it_t1b_tmp->first, it_t1b_tmp->second.first);
                            assert(realPageAtDRAM < DRAM_capacity);
                            t1a.insert(make_pair(it_t1b_tmp->first, make_pair(v_tmp, it_t1b_key_tmp)));
                            ///increase one number of the real pages at DRAM
                            realPageAtDRAM++;
                            t1b.erase(it_t1b_tmp);
                            PRINTV(logfile << "Case II migrate clean page from t1b to t1a: " << it_t1b_tmp->first << "** t1a size: "<< t1a.size()<<", realPageAtDRAM size: "<<realPageAtDRAM<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                        }
                        ///finally
                        ///migrate the hitted page to MRU of t2
                        // Record k as most-recently-used key
                        typename key_tracker_type::iterator itNew = t2_key.insert(t2_key.end(), k);
                        // Create the key-value entry,
                        // linked to the usage record.
                        assert(t2.size()+t1b.size() < (unsigned)NVM_capacity);
                        const V v = _fn(k, value);
                        t2.insert(make_pair(k, make_pair(v, itNew)));
                        PRINTV(logfile << "Case II insert dirty key to t2: " << k << "** t1a size: "<< t1a.size()<<", realPageAtDRAM size: "<<realPageAtDRAM<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                    }
                }
                if(it_t1b != t1b.end()) {
                    PRINTV(logfile << "Case II Write hit on t1b " << k << endl;);
                    ///create a PCR of the hit clean page of t1b
                    value.updateFlags(value.getReq().flags | PCR);
                    const V v_1 = _fn(k, value);
                    ///insert the PCR to t1a
                    typename key_tracker_type::iterator it_t1_key_tmp = t1_key.begin();
                    t1a.insert(make_pair(k, make_pair(v_1, it_t1_key_tmp)));
                    ///evict the hitted clean page from t1b
                    t1b.erase(it_t1b);

                    ///change the hitted clean page's PCR to 0 and insert to MRU of t2
                    value.updateFlags(value.getReq().flags & ~PCR);
                    const V v_2 = _fn(k, value);
                    // Record k as most-recently-used key
                    typename key_tracker_type::iterator itNew = t2_key.insert(t2_key.end(), k);
                    // Create the key-value entry,
                    // linked to the usage record.
                    assert(t2.size()+t1b.size() < (unsigned)NVM_capacity);
                    t2.insert(make_pair(k, make_pair(v_2, itNew)));
                    PRINTV(logfile << "Case II insert dirty key to t2: " << k << "** t1a size: "<< t1a.size()<<", realPageAtDRAM size: "<<realPageAtDRAM<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                }
            }
            ///hit on t1a or t1b, and it is a read request
            else {
                if(it_t1a != t1a.end()) {
                    PRINTV(logfile << "Case II Read hit on t1a " << k << endl;);
                    ///make sure the hitted page is a real page
                    assert( !(it_t1a->second.first.getReq().flags & PCR) );
                    t1a.erase(it_t1a);
                    t1_key.remove(k);
                    realPageAtDRAM--;
                    const V v = _fn(k, value);
                    // Record k as most-recently-used key
                    typename key_tracker_type::iterator itNew
                    = t1_key.insert(t1_key.end(), k);
                    // Create the key-value entry,
                    // linked to the usage record.
                    assert(realPageAtDRAM < DRAM_capacity);
                    t1a.insert(make_pair(k, make_pair(v, itNew)));
                    realPageAtDRAM++;
                    PRINTV(logfile << "Case II insert clean key to t1a: " << k << "** t1a size: "<< t1a.size()<<", realPageAtDRAM size: "<<realPageAtDRAM<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                }
                if(it_t1b != t1b.end()) {
                    PRINTV(logfile << "Case II Read hit on t1b " << k << endl;);
                    ///make sure the hitted page is a real page
                    assert( !(it_t1b->second.first.getReq().flags & PCR) );
                    t1b.erase(it_t1b);
                    t1_key.remove(k);
                    const V v = _fn(k, value);
                    // Record k as most-recently-used key
                    typename key_tracker_type::iterator itNew
                    = t1_key.insert(t1_key.end(), k);
                    // Create the key-value entry,
                    // linked to the usage record.
                    assert(t2.size()+t1b.size() < (unsigned)NVM_capacity);
                    t1b.insert(make_pair(k, make_pair(v, itNew)));
                    PRINTV(logfile << "Case II insert clean key to t1b: " << k << "** t1a size: "<< t1a.size()<<", realPageAtDRAM size: "<<realPageAtDRAM<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                }
            }
            return (status | PAGEHIT | BLKHIT);
        }
        ///*************************************************************************************************
        ///ziqi: HYBRID-Dynamic-WithPCR Case III: x hit in b1, then enlarge t1, and move x from b1 to t1 if it's a read or to t2 if it's a write
        else if(it_b1 != b1.end()) {
            ///ziqi: delta denotes the step in each ADAPTATION
            int delta;

            PRINTV(logfile << "Case III Hit on b1: " << k << endl;);
            if(b1.size() >= b2.size()) {
                delta = 1;
            }
            else {
                if(b1.size() == 0) {
                    delta=10;
                }
                else {
                    delta = int(b2.size()/b1.size());
                }
            }

            PRINTV(logfile << "ADAPTATION: p increases from " << p << " to ";);

            if((p+delta) > int(_capacity))
                p = _capacity;
            else
                p = p+delta;

            PRINTV(logfile << p << endl;);

            const V v = _fn(k, value);
            ///E&B
            REPLACE(k, v, p, status);

            b1.erase(it_b1);
            b1_key.remove(k);

            PRINTV(logfile << "Case III evicts b1 " << k <<  endl;);

            ///ziqi: if it is a write request
            if(status & WRITE) {
                PRINTV(logfile << "Case III write Hit on b1: " << k << endl;);
                ///E&B function must have cleaned a space in t1a instead of t1b, so we need to move a page from t1b to t1a
                if(t2.size()+t1b.size() == (unsigned)NVM_capacity) {
                    typename key_to_value_type::iterator it_t1b_tmp = t1b.begin();
                    assert(it_t1b_tmp != t1b.end());
                    ///make sure the random page from t1b is a real page
                    assert( !(it_t1b_tmp->second.first.getReq().flags & PCR) );
                    typename key_tracker_type::iterator it_t1b_key_tmp = t1_key.begin();
                    const V v_tmp = _fn(it_t1b_tmp->first, it_t1b_tmp->second.first);
                    assert(realPageAtDRAM < DRAM_capacity);
                    t1a.insert(make_pair(it_t1b_tmp->first, make_pair(v_tmp, it_t1b_key_tmp)));
                    realPageAtDRAM++;
                    t1b.erase(it_t1b_tmp);
                    PRINTV(logfile << "Case III migrate clean page from t1b to t1a: " << it_t1b_tmp->first << "** t1a size: "<< t1a.size()<<", realPageAtDRAM size: "<<realPageAtDRAM<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                }
                assert(t2.size()+t1b.size()<(unsigned)NVM_capacity);
                // Record k as most-recently-used key
                typename key_tracker_type::iterator itNew = t2_key.insert(t2_key.end(), k);
                // Create the key-value entry,
                // linked to the usage record.
                assert(t2.size()+t1b.size() < (unsigned)NVM_capacity);
                t2.insert(make_pair(k, make_pair(v, itNew)));
                PRINTV(logfile << "Case III insert a dirty page to t2: " << k << "** t1a size: "<< t1a.size()<<", realPageAtDRAM size: "<<realPageAtDRAM<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
            }
            ///ziqi: if it is a read request
            else {
                PRINTV(logfile << "Case III read Hit on b1: " << k << endl;);
                ///if t1a has space left, always insert clean page to it first
                if(realPageAtDRAM<DRAM_capacity) {
                    // Record k as most-recently-used key
                    typename key_tracker_type::iterator itNew = t1_key.insert(t1_key.end(), k);
                    // Create the key-value entry,
                    // linked to the usage record.
                    assert(realPageAtDRAM < DRAM_capacity);
                    t1a.insert(make_pair(k, make_pair(v, itNew)));
                    realPageAtDRAM++;
                    PRINTV(logfile << "Case III insert a clean page to t1a: " << k << "** t1a size: "<< t1a.size()<<", realPageAtDRAM size: "<<realPageAtDRAM<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                }
                ///if t1a has no space left, use the space in NVRAM for clean, which is t1b
                else {
                    // Record k as most-recently-used key
                    typename key_tracker_type::iterator itNew = t1_key.insert(t1_key.end(), k);
                    // Create the key-value entry,
                    // linked to the usage record.
                    assert(t2.size()+t1b.size() < (unsigned)NVM_capacity);
                    t1b.insert(make_pair(k, make_pair(v, itNew)));
                    PRINTV(logfile << "Case III insert a clean page to t1b: " << k << "** t1a size: "<< t1a.size()<<", realPageAtDRAM size: "<<realPageAtDRAM<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                }

                if(realPageAtDRAM + t1b.size() + b1.size() > _capacity) {
                    if(b1.size() > 0) {
                        typename key_tracker_type::iterator itLRU = b1_key.begin();
                        assert(itLRU != b1_key.end());
                        typename key_to_value_type::iterator it = b1.find(*itLRU);
                        b1.erase(it);
                        b1_key.remove(*itLRU);
                        PRINTV(logfile << "Case III evicts b1 " << *itLRU <<  endl;);
                    }
                    else {
                        PRINTV(logfile << "Case III evicts a real page from t1 starting from LRU position by calling evictRealLRUPageFromT1() " <<  endl;);
                        evictRealLRUPageFromT1();
                    }
                }
            }
            return (status | PAGEMISS);
        }
        ///*************************************************************************************************
        ///ziqi: HYBRID-Dynamic Case IV: x hit in b2, then enlarge t2, and move x from b2 to t1 if it's a read or to t2 if it's a write
        else if(it_b2 != b2.end()) {
            ///ziqi: delta denotes the step in each ADAPTATION
            int delta;

            PRINTV(logfile << "Case IV Hit on b2: " << k << endl;);
            if(b2.size() >= b1.size()) {
                delta = 1;
            }
            else {
                if (b2.size() == 0) {
                    delta = 10;
                }
                else {
                    delta = int(b1.size()/b2.size());
                }
            }

            PRINTV(logfile << "ADAPTATION: p decreases from " << p << " to ";);

            if((p-delta) > 0)
                p = p-delta;
            else
                p = 0;

            PRINTV(logfile << p << endl;);

            const V v = _fn(k, value);

            ///E&B
            REPLACE(k, v, p, status);

            b2.erase(it_b2);
            b2_key.remove(k);

            PRINTV(logfile << "Case IV evicts b2 " << k <<  endl;);

            ///ziqi: if it is a write request
            if(status & WRITE) {
                PRINTV(logfile << "Case IV write Hit on b2: " << k << endl;);
                // Record k as most-recently-used key
                ///E&B function must have cleaned a space in t1a instead of t1b, so we need to move a page from t1b to t1a
                if(t2.size()+t1b.size() == (unsigned)NVM_capacity) {
                    typename key_to_value_type::iterator it_t1b_tmp = t1b.begin();
                    assert(it_t1b_tmp != t1b.end());
                    ///make sure the random page from t1b is a real page
                    assert( !(it_t1b_tmp->second.first.getReq().flags & PCR) );
                    typename key_tracker_type::iterator it_t1b_key_tmp = t1_key.begin();
                    const V v_tmp = _fn(it_t1b_tmp->first, it_t1b_tmp->second.first);
                    assert(realPageAtDRAM < DRAM_capacity);
                    t1a.insert(make_pair(it_t1b_tmp->first, make_pair(v_tmp, it_t1b_key_tmp)));
                    realPageAtDRAM++;
                    t1b.erase(it_t1b_tmp);
                    PRINTV(logfile << "Case IV migrate a clean page from t1b to t1a: " << it_t1b_tmp->first << "** t1a size: "<< t1a.size()<<", realPageAtDRAM size: "<<realPageAtDRAM<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                }
                typename key_tracker_type::iterator itNew = t2_key.insert(t2_key.end(), k);
                // Create the key-value entry,
                // linked to the usage record.
                assert(t2.size()+t1b.size() < (unsigned)NVM_capacity);
                t2.insert(make_pair(k, make_pair(v, itNew)));
                PRINTV(logfile << "Case IV insert a dirty page to t2: " << k << "** t1a size: "<< t1a.size()<<", realPageAtDRAM size: "<<realPageAtDRAM<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
            }
            ///ziqi: if it is a read request
            else {
                PRINTV(logfile << "Case IV read Hit on b2: " << k << endl;);
                ///if t1a has space left, always insert clean page to it first
                if(realPageAtDRAM<DRAM_capacity) {
                    // Record k as most-recently-used key
                    typename key_tracker_type::iterator itNew = t1_key.insert(t1_key.end(), k);
                    // Create the key-value entry,
                    // linked to the usage record.
                    assert(realPageAtDRAM < DRAM_capacity);
                    realPageAtDRAM++;
                    t1a.insert(make_pair(k, make_pair(v, itNew)));
                    PRINTV(logfile << "Case IV insert key to t1a: " << k << "** t1a size: "<< t1a.size()<<", realPageAtDRAM size: "<<realPageAtDRAM<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                }
                ///if t1a has no space left, use the space in NVRAM for clean, which is t1b
                else {
                    // Record k as most-recently-used key
                    typename key_tracker_type::iterator itNew = t1_key.insert(t1_key.end(), k);
                    // Create the key-value entry,
                    // linked to the usage record.
                    assert(t2.size()+t1b.size() < (unsigned)NVM_capacity);
                    t1b.insert(make_pair(k, make_pair(v, itNew)));
                    PRINTV(logfile << "Case IV insert key to t1b: " << k << "** t1a size: "<< t1a.size()<<", realPageAtDRAM size: "<<realPageAtDRAM<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                }

                if(realPageAtDRAM + t1b.size() + b1.size() > _capacity) {
                    if(b1.size() > 0) {
                        typename key_tracker_type::iterator itLRU = b1_key.begin();
                        assert(itLRU != b1_key.end());
                        typename key_to_value_type::iterator it = b1.find(*itLRU);
                        b1.erase(it);
                        b1_key.remove(*itLRU);
                        PRINTV(logfile << "Case IV evicting b1 " << *itLRU <<  endl;);
                    }
                    else {
                        PRINTV(logfile << "Case IV evicts a real page from t1 starting from LRU position by calling evictRealLRUPageFromT1() " <<  endl;);
                        evictRealLRUPageFromT1();
                    }
                }
            }
            return (status | PAGEMISS);
        }
        ///*************************************************************************************************
        ///ziqi: HYBRID-Dynamic Case V: x is cache miss
        else {
            PRINTV(logfile << "Case V miss on key: " << k << endl;);

            if((realPageAtDRAM + t1b.size() + b1.size()) == _capacity) {
                ///b1 is not empty
                if(realPageAtDRAM + t1b.size() < _capacity) {
                    typename key_tracker_type::iterator itLRU = b1_key.begin();
                    assert(itLRU != b1_key.end());
                    typename key_to_value_type::iterator it = b1.find(*itLRU);
                    b1.erase(it);
                    b1_key.remove(*itLRU);
                    const V v = _fn(k, value);
                    ///E&B
                    REPLACE(k, v, p, status);
                }
                ///b1 is empty
                else {
                    PRINTV(logfile << "Case V evicts a real page from t1 starting from LRU position by calling evictRealLRUPageFromT1()" <<  endl;);
                    evictRealLRUPageFromT1();
                }
            }
            else if((realPageAtDRAM + t1b.size() + b1.size()) < _capacity) {
                if((realPageAtDRAM + t1b.size() + t2.size() + b1.size() + b2.size()) >= _capacity) {
                    if((realPageAtDRAM + t1b.size() + t2.size() + b1.size() + b2.size()) == 2 * _capacity) {
                        typename key_tracker_type::iterator itLRU = b2_key.begin();
                        assert(itLRU != b2_key.end());
                        typename key_to_value_type::iterator it = b2.find(*itLRU);
                        b2.erase(it);
                        b2_key.remove(*itLRU);
                    }
                    const V v = _fn(k, value);
                    ///E&B
                    REPLACE(k, v, p, status);
                }
            }
            const V v = _fn(k, value);
            ///write request
            if(status & WRITE) {
                // Record k as most-recently-used key
                typename key_tracker_type::iterator itNew = t2_key.insert(t2_key.end(), k);
                ///NVRAM is not full
                if(t1b.size() + t2.size() < (unsigned)NVM_capacity) {
                    PRINTV(logfile << "Case V, write miss and NVRAM not full: " << k << endl;);
                    // Create the key-value entry,
                    // linked to the usage record.
                    assert(t2.size()+t1b.size() < (unsigned)NVM_capacity);
                    t2.insert(make_pair(k, make_pair(v, itNew)));
                    PRINTV(logfile << "Case V insert dirty key to t2: " << k << "** t1a size: "<< t1a.size()<<", realPageAtDRAM size: "<<realPageAtDRAM<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                }
                ///NVRAM is full
                else {
                    ///NVRAM is filled with dirty pages
                    if(t2.size() == (unsigned)NVM_capacity) {
                        PRINTV(logfile << "Case V, write miss and NVRAM is full, t2.size() == NVRAM: " << k << endl;);
                        ///select the LRU page of t2
                        typename key_tracker_type::iterator itLRU = t2_key.begin();
                        assert(itLRU != t2_key.end());
                        typename key_to_value_type::iterator it = t2.find(*itLRU);
                        assert(it != t2.end());
                        ///check whether the LRU page of t2 has a PCR at t1a
                        typename key_to_value_type::iterator itPCRatDRAM = t1a.find(*itLRU);
                        ///if the LRU page of t2 has a PCR at t1a,
                        ///change the PCR to a real page
                        if(itPCRatDRAM != t1a.end()) {
                            ///make sure it is a PCR instead of a real page
                            assert(itPCRatDRAM->second.first.getReq().flags & PCR);
                            PRINTV(logfile << "Change the PCR of LRU page of t2 from a PCR to a real page " <<endl;);
                            assert(realPageAtDRAM < DRAM_capacity);
                            PRINTV(logfile << "Key status before change: " << bitset<14>(itPCRatDRAM->second.first.getReq().flags) << endl;);
                            itPCRatDRAM->second.first.updateFlags(itPCRatDRAM->second.first.getReq().flags & ~PCR);
                            PRINTV(logfile << "Key status after change: " << bitset<14>(itPCRatDRAM->second.first.getReq().flags) << endl;);
                            realPageAtDRAM++;
                        }
                        ///if the LRU page of t2 has no PCR at t1a
                        ///insert the LRU page for t2 to t1a, since t1a has at least one page space left
                        ///Note that we insert the page from t2 to the LRU position of t1, it is NOT MRU position
                        else {
                            typename key_tracker_type::iterator itNewTmp = t1_key.insert(t1_key.begin(), *itLRU);
                            // Create the key-value entry,
                            // linked to the usage record.
                            const V v_tmp = _fn(*itLRU, it->second.first);
                            assert(realPageAtDRAM < DRAM_capacity);
                            t1a.insert(make_pair(*itLRU, make_pair(v_tmp, itNewTmp)));
                            realPageAtDRAM++;
                            PRINTV(logfile << "Case V insert the LRU page of t2 to LRU position of t1 on t1a: " << *itLRU << "** t1a size: "<< t1a.size()<<", realPageAtDRAM size: "<<realPageAtDRAM<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                        }

                        ///check whether LRU page of t2 exists in migrated list
                        typename migrated_list_type::iterator itMigratedList = find(migrated_list.begin(), migrated_list.end(), *itLRU);
                        if(itMigratedList != migrated_list.end()) {
                            totalPageWriteToStorageWithPingpong++;
                            PRINTV(logfile << "migrated_list contains key: " << *itLRU << "** totalPageWriteToStorageWithPingpong size: "<< totalPageWriteToStorageWithPingpong <<endl;);
                        }
                        ///insert the LRU page of t2 to migrated list
                        if(itMigratedList == migrated_list.end()) {
                            migrated_list.insert(migrated_list.begin(), *itLRU);
                            PRINTV(logfile << "migrated_list added key: " << *itLRU << "** migrated_list size: "<< migrated_list.size() <<endl;);
                        }
                        ///flush back and evcit the LRU page of t2
                        t2.erase(it);
                        t2_key.remove(*itLRU);
                        PRINTV(logfile << "Case V (NVM is filled with dirty pages) evicts and flushes a dirty page " << *itLRU << "** t1a size: "<< t1a.size()<<", realPageAtDRAM size: "<<realPageAtDRAM<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                        totalPageWriteToStorage++;

                        ///insert the missed page to MRU of t2
                        // Record k as most-recently-used key
                        typename key_tracker_type::iterator itNew = t2_key.insert(t2_key.end(), k);
                        // Create the key-value entry,
                        // linked to the usage record.
                        assert(t2.size()+t1b.size() < (unsigned)NVM_capacity);
                        t2.insert(make_pair(k, make_pair(v, itNew)));
                        PRINTV(logfile << "Case V insert a dirty page to t2: " << k << "** t1a size: "<< t1a.size()<<", realPageAtDRAM size: "<<realPageAtDRAM<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                    }
                    ///NVRAM is full but is not filled with dirty pages
                    else { ///t2.size() < (unsigned)NVM_capacity
                        PRINTV(logfile << "Case V, write miss and NVRAM is full, t2.size() < NVRAM: " << k << endl;);
                        ///migrate a clean page from t1b to t1a
                        typename key_to_value_type::iterator it_t1b_tmp = t1b.begin();
                        assert(it_t1b_tmp != t1b.end());
                        ///make sure the random page from t1b is a real page
                        assert( !(it_t1b_tmp->second.first.getReq().flags & PCR) );
                        typename key_tracker_type::iterator it_t1b_key_tmp = t1_key.begin();
                        const V v_tmp = _fn(it_t1b_tmp->first, it_t1b_tmp->second.first);
                        assert(realPageAtDRAM < DRAM_capacity);
                        t1a.insert(make_pair(it_t1b_tmp->first, make_pair(v_tmp, it_t1b_key_tmp)));
                        realPageAtDRAM++;
                        t1b.erase(it_t1b_tmp);
                        PRINTV(logfile << "Case V migrate a clean page from t1b to t1a: " << it_t1b_tmp->first << "** t1a size: "<< t1a.size()<<", realPageAtDRAM size: "<<realPageAtDRAM<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                        ///insert the missed page to MRU of t2
                        // Record k as most-recently-used key
                        typename key_tracker_type::iterator itNew = t2_key.insert(t2_key.end(), k);
                        // Create the key-value entry,
                        // linked to the usage record.
                        assert(t2.size()+t1b.size() < (unsigned)NVM_capacity);
                        t2.insert(make_pair(k, make_pair(v, itNew)));
                        PRINTV(logfile << "Case V insert a dirty page to t2: " << k << "** t1a size: "<< t1a.size()<<", realPageAtDRAM size: "<<realPageAtDRAM<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                    }
                }
            }
            ///read request
            else {
                // Record k as most-recently-used key
                typename key_tracker_type::iterator itNew = t1_key.insert(t1_key.end(), k);
                // Create the key-value entry,
                // linked to the usage record.
                if(realPageAtDRAM < DRAM_capacity) {
                    PRINTV(logfile << "Case V, read miss and DRAM is not full: " << k << endl;);
                    assert(realPageAtDRAM < DRAM_capacity);
                    t1a.insert(make_pair(k, make_pair(v, itNew)));
                    realPageAtDRAM++;
                    PRINTV(logfile << "Case V insert a clean page to t1a: " << k << "** t1a size: "<< t1a.size()<<", realPageAtDRAM size: "<<realPageAtDRAM<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                }
                else {
                    PRINTV(logfile << "Case V, read miss and DRAM is full and NVRAM is not full: " << k << endl;);
                    assert(t2.size()+t1b.size() < (unsigned)NVM_capacity);
                    t1b.insert(make_pair(k, make_pair(v, itNew)));
                    PRINTV(logfile << "Case V insert a clean page to t1b: " << k << "** t1a size: "<< t1a.size()<<", realPageAtDRAM size: "<<realPageAtDRAM<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                }
            }
            return (status | PAGEMISS);
        }
        ///should never reach here
        assert(0);
        return 0;
    } //end operator access
    ///*************************************************************************************************
    ///ziqi: HYBRID-Dynamic-WithPCR subroutine
    void REPLACE(const K &k, const V &v, int p, uint32_t status) {
        ///REPLACE can only be triggered only if both NVRAM and DRAM are full
        assert(realPageAtDRAM + t1b.size() + t2.size() == _capacity);

        size_t DRAM_capacity = (size_t)_capacity * moneyAllo4D;
        int NVM_capacity = (int)_capacity * (1 - moneyAllo4D) * priceDvsN;

        ///Delete this if priceDvsN is not equal to 1.0
        if(DRAM_capacity + NVM_capacity < _capacity) {
            NVM_capacity = _capacity - DRAM_capacity;
        }
        ///if NVRAM is not filled with dirty pages, do as the usual case
        if(t2.size() < (unsigned)NVM_capacity) {
            PRINTV(logfile << "REPLACE in normal case that NVRAM is not filled with dirty page" << endl;);
            typename key_to_value_type::iterator it = b2.find(k);
            ///evict from clean cache side
            if((realPageAtDRAM+t1b.size() > 0) && ( (DRAM_capacity + t1b.size() > unsigned(p) ) || ((it != b2.end()) && (DRAM_capacity + t1b.size() == unsigned(p))))) {
                PRINTV(logfile << "REPLACE from clean page cache" << endl;);
                typename key_tracker_type::iterator itLRU = t1_key.begin();
                assert(itLRU != t1_key.end());
                typename key_to_value_type::iterator it_t1a = t1a.find(*itLRU);
                typename key_to_value_type::iterator it_t1b = t1b.find(*itLRU);

                while(itLRU != t1_key.end()) {
                    itLRU = t1_key.begin();
                    it_t1a = t1a.find(*itLRU);
                    it_t1b = t1b.find(*itLRU);
                    ///the LRU page at t1a
                    if(it_t1a != t1a.end()) {
                        assert(it_t1b == t1b.end());
                        ///ziqi: if PCR of the page at LRU position of lru list for read is 1, it is a ghost, evcit it and continue
                        if((it_t1a->second.first.getReq().flags) & PCR) {
                            t1a.erase(it_t1a);
                            t1_key.remove(*itLRU);
                            PRINTV(logfile << "REPALCE evitcs a PCR from t1a: " << *itLRU << "** t1a size: "<< t1a.size()<<", realPageAtDRAM size: "<<realPageAtDRAM<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                        }
                        ///ziqi: if PCR of the page at LRU position of lru list for read is 0, it is a real page, evcit it and break
                        else {
                            ///insert the LRU page of t1 to b1
                            typename key_tracker_type::iterator itNew = b1_key.insert(b1_key.end(), *itLRU);
                            // Create the key-value entry,
                            // linked to the usage record.
                            const V v_tmp = _fn(*itLRU, it_t1a->second.first);
                            b1.insert(make_pair(*itLRU, make_pair(v_tmp, itNew)));
                            PRINTV(logfile << "REPLACE insert key to b1: " << *itLRU <<  "** b1 size: "<< b1.size() << endl;);
                            ///evict the LRU page of t1_key
                            t1a.erase(it_t1a);
                            t1_key.remove(*itLRU);

                            ///if the LRU page of t1 exists in migrated_list, evcit it
                            typename migrated_list_type::iterator itMigratedList = find(migrated_list.begin(), migrated_list.end(), *itLRU);
                            if(itMigratedList != migrated_list.end()) {
                                migrated_list.remove(*itLRU);
                                PRINTV(logfile << "migrated_list removes key: " << *itLRU << "** migrated_list size: "<< migrated_list.size() <<endl;);
                            }

                            realPageAtDRAM--;
                            PRINTV(logfile << "REPALCE evitcs a real page from t1a: " << *itLRU << "** t1a size: "<< t1a.size()<<", realPageAtDRAM size: "<<realPageAtDRAM<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                            break;
                        }
                    }
                    ///the LRU page at t1b
                    if(it_t1b != t1b.end()) {
                        ///make sure the evicted page from t1b is a real page
                        assert( !(it_t1b->second.first.getReq().flags & PCR) );
                        assert(it_t1a == t1a.end());
                        ///insert the LRU page of t1 to b1
                        typename key_tracker_type::iterator itNew = b1_key.insert(b1_key.end(), *itLRU);
                        // Create the key-value entry,
                        // linked to the usage record.
                        const V v_tmp = _fn(*itLRU, it_t1a->second.first);
                        b1.insert(make_pair(*itLRU, make_pair(v_tmp, itNew)));
                        PRINTV(logfile << "REPLACE insert key to b1: " << *itLRU <<  "** b1 size: "<< b1.size() << endl;);

                        t1b.erase(it_t1b);
                        t1_key.remove(*itLRU);

                        ///if the LRU page of t1 exists in migrated_list, evcit it
                        typename migrated_list_type::iterator itMigratedList = find(migrated_list.begin(), migrated_list.end(), *itLRU);
                        if(itMigratedList != migrated_list.end()) {
                            migrated_list.remove(*itLRU);
                            PRINTV(logfile << "migrated_list removes key: " << *itLRU << "** migrated_list size: "<< migrated_list.size() <<endl;);
                        }

                        PRINTV(logfile << "REPALCE evitcs a real page from t1b: " << *itLRU << "** t1a size: "<< t1a.size()<<", realPageAtDRAM size: "<<realPageAtDRAM<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                        break;
                    }
                }
            }
            ///evcit from dirty cache side
            else {
                PRINTV(logfile << "REPLACE from dirty page cache" << endl;);
                typename key_tracker_type::iterator itLRU_t2 = t2_key.begin();
                assert(itLRU_t2 != t2_key.end());
                typename key_to_value_type::iterator itLRUValue_t2 = t2.find(*itLRU_t2);
                typename key_to_value_type::iterator itPCRatDRAM = t1a.find(*itLRU_t2);

                ///the LRU page in t2 has a PCR at t1a
                if(itPCRatDRAM != t1a.end()) {
                    ///make sure itPCRatDRAM from t1a is a PCR
                    assert( itPCRatDRAM->second.first.getReq().flags & PCR );
                    typename key_tracker_type::iterator itLRU_t1 = t1_key.begin();
                    assert(itLRU_t1 != t1_key.end());

                    typename key_to_value_type::iterator it_t1a = t1a.find(*itLRU_t1);
                    typename key_to_value_type::iterator it_t1b = t1b.find(*itLRU_t1);

                    while(itLRU_t1 != t1_key.end()) {
                        itLRU_t1 = t1_key.begin();
                        it_t1a = t1a.find(*itLRU_t1);
                        it_t1b = t1b.find(*itLRU_t1);
                        ///the LRU page at t1b
                        if(it_t1b != t1b.end()) {
                            PRINTV(logfile << "the LRU page of t1_key is on t1b and it must be a real page, evict it and move the LRU page of t2 to t1b " << endl;);
                            ///make sure the evicted page from t1b is a real page
                            assert( !(it_t1b->second.first.getReq().flags & PCR) );
                            assert(it_t1a == t1a.end());
                            ///evict the real page in t1b
                            t1b.erase(it_t1b);
                            t1_key.remove(*itLRU_t1);

                            ///if the LRU page of t1 exists in migrated_list, evcit it
                            typename migrated_list_type::iterator itMigratedList = find(migrated_list.begin(), migrated_list.end(), *itLRU_t1);
                            if(itMigratedList != migrated_list.end()) {
                                migrated_list.remove(*itLRU_t1);
                                PRINTV(logfile << "migrated_list removes key: " << *itLRU_t1 << "** migrated_list size: "<< migrated_list.size() <<endl;);
                            }

                            ///evict the PCR from t1a, but keep its key in t1_key
                            t1a.erase(itPCRatDRAM);
                            ///insert the LRU page of t2 to t1b
                            const V v_tmp = _fn(*itLRU_t2, itLRUValue_t2->second.first);
                            t1b.insert(make_pair(*itLRU_t2, make_pair(v_tmp, itLRU_t2)));
                            ///insert the LRU page of t2 to b2
                            typename key_tracker_type::iterator itNew = b2_key.insert(b2_key.end(), *itLRU_t2);
                            // Create the key-value entry,
                            // linked to the usage record.
                            b2.insert(make_pair(*itLRU_t2, make_pair(v_tmp, itNew)));
                            PRINTV(logfile << "REPLACE insert key to b2: " << *itLRU_t2 <<  "** b2 size: "<< b2.size() << endl;);
                            ///evict the LRU page of t2
                            t2.erase(itLRUValue_t2);
                            t2_key.remove(*itLRU_t2);
                            totalPageWriteToStorage++;

                            ///check whether LRU page of t2 exists in migrated list
                            itMigratedList = find(migrated_list.begin(), migrated_list.end(), *itLRU_t2);
                            if(itMigratedList != migrated_list.end()) {
                                totalPageWriteToStorageWithPingpong++;
                                PRINTV(logfile << "migrated_list contains key: " << *itLRU_t2 << "** totalPageWriteToStorageWithPingpong size: "<< totalPageWriteToStorageWithPingpong <<endl;);
                                migrated_list.remove(*itLRU_t2);
                                PRINTV(logfile << "migrated_list removes key: " << *itLRU_t2 << "** migrated_list size: "<< migrated_list.size() <<endl;);
                            }

                            PRINTV(logfile << "REPALCE evitcs a dirty page from t2: " << *itLRU_t2 << "** t1a size: "<< t1a.size()<<", realPageAtDRAM size: "<<realPageAtDRAM<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                            break;
                        }
                        ///the LRU page at t1a
                        if(it_t1a != t1a.end()) {
                            if( (it_t1a->second.first.getReq().flags & PCR) == 0) {
                                PRINTV(logfile << "the LRU page of t1_key is on t1a and it is a real page, evict it and move the LRU page of t2 to t1a " << endl;);
                                ///evict the LRU real page from t1a
                                t1a.erase(it_t1a);
                                t1_key.remove(*itLRU_t1);
                                realPageAtDRAM--;

                                ///if the LRU page of t1 exists in migrated_list, evcit it
                                typename migrated_list_type::iterator itMigratedList = find(migrated_list.begin(), migrated_list.end(), *itLRU_t1);
                                if(itMigratedList != migrated_list.end()) {
                                    migrated_list.remove(*itLRU_t1);
                                    PRINTV(logfile << "migrated_list removes key: " << *itLRU_t1 << "** migrated_list size: "<< migrated_list.size() <<endl;);
                                }

                                ///change LRU page of t2's PCR to a real page
                                PRINTV(logfile << "Key status before change: " << bitset<14>(itPCRatDRAM->second.first.getReq().flags) << endl;);
                                itPCRatDRAM->second.first.updateFlags(itPCRatDRAM->second.first.getReq().flags & ~PCR);
                                PRINTV(logfile << "Key status after change: " << bitset<14>(itPCRatDRAM->second.first.getReq().flags) << endl;);
                                realPageAtDRAM++;
                                ///insert the LRU page of t2 to b2
                                const V v_tmp = _fn(*itLRU_t2, itLRUValue_t2->second.first);
                                typename key_tracker_type::iterator itNew = b2_key.insert(b2_key.end(), *itLRU_t2);
                                // Create the key-value entry,
                                // linked to the usage record.
                                b2.insert(make_pair(*itLRU_t2, make_pair(v_tmp, itNew)));
                                PRINTV(logfile << "REPLACE insert key to b2: " << *itLRU_t2 <<  "** b2 size: "<< b2.size() << endl;);
                                ///evict the LRU page of t2
                                t2.erase(itLRUValue_t2);
                                t2_key.remove(*itLRU_t2);
                                totalPageWriteToStorage++;

                                ///check whether LRU page of t2 exists in migrated list
                                itMigratedList = find(migrated_list.begin(), migrated_list.end(), *itLRU_t2);
                                if(itMigratedList != migrated_list.end()) {
                                    totalPageWriteToStorageWithPingpong++;
                                    PRINTV(logfile << "migrated_list contains key: " << *itLRU_t2 << "** totalPageWriteToStorageWithPingpong size: "<< totalPageWriteToStorageWithPingpong <<endl;);
                                }
                                ///insert the LRU page of t2 to migrated list
                                if(itMigratedList == migrated_list.end()) {
                                    migrated_list.insert(migrated_list.begin(), *itLRU_t2);
                                    PRINTV(logfile << "migrated_list added key: " << *itLRU_t2 << "** migrated_list size: "<< migrated_list.size() <<endl;);
                                }

                                break;
                            }
                            else if(*itLRU_t1 == *itLRU_t2) {
                                PRINTV(logfile << "the LRU page of t1_key is the same as the LRU page of t2_key, evict both of them out of cache " << endl;);
                                assert(it_t1a->second.first.getReq().flags & PCR);
                                assert(*itLRU_t1 == itPCRatDRAM->first);
                                ///evict PCR from DRAM
                                t1a.erase(itPCRatDRAM);
                                t1_key.remove(*itLRU_t2);
                                ///insert the LRU page of t2 to b2
                                const V v_tmp = _fn(*itLRU_t2, itLRUValue_t2->second.first);
                                typename key_tracker_type::iterator itNew = b2_key.insert(b2_key.end(), *itLRU_t2);
                                // Create the key-value entry,
                                // linked to the usage record.
                                b2.insert(make_pair(*itLRU_t2, make_pair(v_tmp, itNew)));
                                PRINTV(logfile << "REPLACE insert key to b2: " << *itLRU_t2 <<  "** b2 size: "<< b2.size() << endl;);
                                ///evict the LRU page of t2
                                t2.erase(itLRUValue_t2);
                                t2_key.remove(*itLRU_t2);
                                totalPageWriteToStorage++;

                                ///check whether LRU page of t2 exists in migrated list
                                typename migrated_list_type::iterator itMigratedList = find(migrated_list.begin(), migrated_list.end(), *itLRU_t2);
                                if(itMigratedList != migrated_list.end()) {
                                    totalPageWriteToStorageWithPingpong++;
                                    PRINTV(logfile << "migrated_list contains key: " << *itLRU_t2 << "** totalPageWriteToStorageWithPingpong size: "<< totalPageWriteToStorageWithPingpong <<endl;);
                                    migrated_list.remove(*itLRU_t2);
                                    PRINTV(logfile << "migrated_list removes key: " << *itLRU_t2 << "** migrated_list size: "<< migrated_list.size() <<endl;);
                                }

                                break;
                            }
                            else if( ((it_t1a->second.first.getReq().flags & PCR) ) && (*itLRU_t1 != *itLRU_t2) ) {
                                PRINTV(logfile << "the LRU page of t1_key is a PCR and not LRU page of t2_key, evict the PCR and continue " << endl;);
                                assert(it_t1a->second.first.getReq().flags & PCR);
                                ///evict PCR from DRAM
                                t1a.erase(it_t1a);
                                t1_key.remove(*itLRU_t1);
                                continue;
                            }
                        }
                    }
                }
                ///the LRU page in t2 does not have a PCR at t1a
                else {
                    PRINTV(logfile << "the LRU page of t2 does not have a PCR, evict the LRU page of t2 " << endl;);
                    ///insert the LRU page of t2 to b2
                    const V v_tmp = _fn(*itLRU_t2, itLRUValue_t2->second.first);
                    typename key_tracker_type::iterator itNew = b2_key.insert(b2_key.end(), *itLRU_t2);
                    // Create the key-value entry,
                    // linked to the usage record.
                    b2.insert(make_pair(*itLRU_t2, make_pair(v_tmp, itNew)));
                    PRINTV(logfile << "REPLACE insert a dirty page to b2: " << *itLRU_t2 <<  "** b2 size: "<< b2.size() << endl;);
                    ///evict the LRU page of t2
                    t2.erase(itLRUValue_t2);
                    t2_key.remove(*itLRU_t2);
                    totalPageWriteToStorage++;

                    ///check whether LRU page of t2 exists in migrated list
                    typename migrated_list_type::iterator itMigratedList = find(migrated_list.begin(), migrated_list.end(), *itLRU_t2);
                    if(itMigratedList != migrated_list.end()) {
                        totalPageWriteToStorageWithPingpong++;
                        PRINTV(logfile << "migrated_list contains key: " << *itLRU_t2 << "** totalPageWriteToStorageWithPingpong size: "<< totalPageWriteToStorageWithPingpong <<endl;);
                        migrated_list.remove(*itLRU_t2);
                        PRINTV(logfile << "migrated_list removes key: " << *itLRU_t2 << "** migrated_list size: "<< migrated_list.size() <<endl;);
                    }
                }
            }
        }
        ///if NVM is filled with dirty pages,
        if(t2.size() == (unsigned)NVM_capacity) {
            PRINTV(logfile << "REPLACE in abnormal case that NVRAM is filled with dirty page" << endl;);
            typename key_to_value_type::iterator it = b2.find(k);
            ///if we decide to evict from clean cache and the request is a read request, then evict the LRU page in t1a
            if( (status & READ) && ( (realPageAtDRAM+t1b.size() > 0) && ( (DRAM_capacity + t1b.size() > unsigned(p) ) || (it != b2.end() && (DRAM_capacity + t1b.size() == unsigned(p) ) ) ) ) ) {
                PRINTV(logfile << "REPLACE from clean page cache" << endl;);
                typename key_tracker_type::iterator itLRU = t1_key.begin();
                assert(itLRU != t1_key.end());
                typename key_to_value_type::iterator it_t1a = t1a.find(*itLRU);
                assert(it_t1a != t1a.end());
                typename key_to_value_type::iterator it_t1b = t1b.find(*itLRU);
                ///t1b should be empty and the key should not be found in t1b
                assert(it_t1b == t1b.end());

                while(itLRU != t1_key.end()) {
                    itLRU = t1_key.begin();
                    it_t1a = t1a.find(*itLRU);
                    it_t1b = t1b.find(*itLRU);
                    ///the LRU page at t1a
                    assert(it_t1a != t1a.end());
                    ///t1b should be empty and the key should not be found in t1b
                    assert(it_t1b == t1b.end());
                    ///ziqi: if PCR of the page at LRU position of lru list for read is 1, it is a ghost, evcit it and continue
                    if((it_t1a->second.first.getReq().flags) & PCR) {
                        t1a.erase(it_t1a);
                        t1_key.remove(*itLRU);
                        PRINTV(logfile << "REPALCE evitcs a PCR from t1a: " << *itLRU << "** t1a size: "<< t1a.size()<<", realPageAtDRAM size: "<<realPageAtDRAM<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                    }
                    ///ziqi: if PCR of the page at LRU position of lru list for read is 0, it is a read page, evcit it and break
                    else {
                        ///insert the LRU page of t1 to b1
                        typename key_tracker_type::iterator itNew = b1_key.insert(b1_key.end(), *itLRU);
                        // Create the key-value entry,
                        // linked to the usage record.
                        const V v_tmp = _fn(*itLRU, it_t1a->second.first);
                        b1.insert(make_pair(*itLRU, make_pair(v_tmp, itNew)));
                        PRINTV(logfile << "REPLACE insert key to b1: " << *itLRU <<  "** b1 size: "<< b1.size() << endl;);
                        ///evict the LRU page of t1_key
                        t1a.erase(it_t1a);
                        t1_key.remove(*itLRU);
                        realPageAtDRAM--;

                        ///if the LRU page of t1 exists in migrated_list, evcit it
                        typename migrated_list_type::iterator itMigratedList = find(migrated_list.begin(), migrated_list.end(), *itLRU);
                        if(itMigratedList != migrated_list.end()) {
                            migrated_list.remove(*itLRU);
                            PRINTV(logfile << "migrated_list removes key: " << *itLRU << "** migrated_list size: "<< migrated_list.size() <<endl;);
                        }

                        PRINTV(logfile << "REPALCE evitcs a real page from t1a: " << *itLRU << "** t1a size: "<< t1a.size()<<", realPageAtDRAM size: "<<realPageAtDRAM<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                        break;
                    }
                }
            }
            ///evcit the LRU page from dirty cache side
            else {
                PRINTV(logfile << "REPLACE from dirty page cache" << endl;);
                typename key_tracker_type::iterator itLRU_t2 = t2_key.begin();
                assert(itLRU_t2 != t2_key.end());
                typename key_to_value_type::iterator itLRUValue_t2 = t2.find(*itLRU_t2);
                typename key_to_value_type::iterator itPCRatDRAM = t1a.find(*itLRU_t2);

                ///the LRU page in t2 has a PCR at t1a
                if(itPCRatDRAM != t1a.end()) {
                    ///make sure itPCRatDRAM from t1a is a PCR
                    assert( itPCRatDRAM->second.first.getReq().flags & PCR );
                    typename key_tracker_type::iterator itLRU_t1 = t1_key.begin();
                    assert(itLRU_t1 != t1_key.end());

                    typename key_to_value_type::iterator it_t1a = t1a.find(*itLRU_t1);
                    typename key_to_value_type::iterator it_t1b = t1b.find(*itLRU_t1);

                    while(itLRU_t1 != t1_key.end()) {
                        itLRU_t1 = t1_key.begin();
                        it_t1a = t1a.find(*itLRU_t1);
                        it_t1b = t1b.find(*itLRU_t1);
                        ///the LRU page must at t1a since t1b has no clean pages now
                        assert(it_t1a != t1a.end());
                        ///t1b should be empty and the key should not be found in t1b
                        assert(it_t1b == t1b.end());

                        if( (it_t1a->second.first.getReq().flags & PCR) == 0) {
                            PRINTV(logfile << "the LRU page of t1_key can only on t1a, evict and move the LRU page of t2 to t1a " << endl;);
                            ///evict the LRU real page from t1a
                            t1a.erase(it_t1a);
                            t1_key.remove(*itLRU_t1);
                            realPageAtDRAM--;

                            ///if the LRU page of t1 exists in migrated_list, evcit it
                            typename migrated_list_type::iterator itMigratedList = find(migrated_list.begin(), migrated_list.end(), *itLRU_t1);
                            if(itMigratedList != migrated_list.end()) {
                                migrated_list.remove(*itLRU_t1);
                                PRINTV(logfile << "migrated_list removes key: " << *itLRU_t1 << "** migrated_list size: "<< migrated_list.size() <<endl;);
                            }

                            ///change PCR to a real page
                            PRINTV(logfile << "Key status before change: " << bitset<14>(itPCRatDRAM->second.first.getReq().flags) << endl;);
                            itPCRatDRAM->second.first.updateFlags(itPCRatDRAM->second.first.getReq().flags & ~PCR);
                            PRINTV(logfile << "Key status after change: " << bitset<14>(itPCRatDRAM->second.first.getReq().flags) << endl;);
                            realPageAtDRAM++;
                            ///insert the LRU page of t2 to b2
                            const V v_tmp = _fn(*itLRU_t2, itLRUValue_t2->second.first);
                            typename key_tracker_type::iterator itNew = b2_key.insert(b2_key.end(), *itLRU_t2);
                            // Create the key-value entry,
                            // linked to the usage record.
                            b2.insert(make_pair(*itLRU_t2, make_pair(v_tmp, itNew)));
                            PRINTV(logfile << "REPLACE insert key to b2: " << *itLRU_t2 <<  "** b2 size: "<< b2.size() << endl;);
                            ///evict the LRU page of t2
                            t2.erase(itLRUValue_t2);
                            t2_key.remove(*itLRU_t2);
                            totalPageWriteToStorage++;

                            ///check whether LRU page of t2 exists in migrated list
                            itMigratedList = find(migrated_list.begin(), migrated_list.end(), *itLRU_t2);
                            if(itMigratedList != migrated_list.end()) {
                                totalPageWriteToStorageWithPingpong++;
                                PRINTV(logfile << "migrated_list contains key: " << *itLRU_t2 << "** totalPageWriteToStorageWithPingpong size: "<< totalPageWriteToStorageWithPingpong <<endl;);
                            }
                            ///insert the LRU page of t2 to migrated list
                            if(itMigratedList == migrated_list.end()) {
                                migrated_list.insert(migrated_list.begin(), *itLRU_t2);
                                PRINTV(logfile << "migrated_list added key: " << *itLRU_t2 << "** migrated_list size: "<< migrated_list.size() <<endl;);
                            }

                            break;
                        }
                        else if(*itLRU_t1 == *itLRU_t2) {
                            PRINTV(logfile << "the LRU page of t1_key can only on t1a, the LRU page of t1_key is the same as t2_key, evict them both " << endl;);
                            assert(it_t1a->second.first.getReq().flags & PCR);
                            assert(*itLRU_t1 == itPCRatDRAM->first);
                            ///evict PCR from DRAM
                            t1a.erase(itPCRatDRAM);
                            t1_key.remove(*itLRU_t2);
                            ///insert the LRU page of t2 to b2
                            const V v_tmp = _fn(*itLRU_t2, itLRUValue_t2->second.first);
                            typename key_tracker_type::iterator itNew = b2_key.insert(b2_key.end(), *itLRU_t2);
                            // Create the key-value entry,
                            // linked to the usage record.
                            b2.insert(make_pair(*itLRU_t2, make_pair(v_tmp, itNew)));
                            PRINTV(logfile << "REPLACE insert key to b2: " << *itLRU_t2 <<  "** b2 size: "<< b2.size() << endl;);
                            ///evict the LRU page of t2
                            t2.erase(itLRUValue_t2);
                            t2_key.remove(*itLRU_t2);
                            totalPageWriteToStorage++;

                            ///check whether LRU page of t2 exists in migrated list
                            typename migrated_list_type::iterator itMigratedList = find(migrated_list.begin(), migrated_list.end(), *itLRU_t2);
                            if(itMigratedList != migrated_list.end()) {
                                totalPageWriteToStorageWithPingpong++;
                                PRINTV(logfile << "migrated_list contains key: " << *itLRU_t2 << "** totalPageWriteToStorageWithPingpong size: "<< totalPageWriteToStorageWithPingpong <<endl;);
                                migrated_list.remove(*itLRU_t2);
                                PRINTV(logfile << "migrated_list removes key: " << *itLRU_t2 << "** migrated_list size: "<< migrated_list.size() <<endl;);
                            }

                            break;
                        }
                        else if( ((it_t1a->second.first.getReq().flags & PCR) ) && (*itLRU_t1 != *itLRU_t2) ) {
                            PRINTV(logfile << "the LRU page of t1_key can only on t1a, the LRU page of t1_key is a PCR but not LRU page of t2_key, evict the PCR and continue " << endl;);
                            assert(it_t1a->second.first.getReq().flags & PCR);
                            ///evict PCR from DRAM
                            t1a.erase(it_t1a);
                            t1_key.remove(*itLRU_t1);
                        }
                    }
                }
                ///the LRU page in t2 does not have a PCR at t1a
                else {
                    PRINTV(logfile << "the LRU page of t2 does not have a PCR, evict the LRU page of t2 " << endl;);
                    ///insert the LRU page of t2 to b2
                    const V v_tmp = _fn(*itLRU_t2, itLRUValue_t2->second.first);
                    typename key_tracker_type::iterator itNew = b2_key.insert(b2_key.end(), *itLRU_t2);
                    // Create the key-value entry,
                    // linked to the usage record.
                    b2.insert(make_pair(*itLRU_t2, make_pair(v_tmp, itNew)));
                    PRINTV(logfile << "REPLACE insert key to b2: " << *itLRU_t2 <<  "** b2 size: "<< b2.size() << endl;);
                    ///evict the LRU page of t2
                    t2.erase(itLRUValue_t2);
                    t2_key.remove(*itLRU_t2);
                    totalPageWriteToStorage++;

                    ///check whether LRU page of t2 exists in migrated list
                    typename migrated_list_type::iterator itMigratedList = find(migrated_list.begin(), migrated_list.end(), *itLRU_t2);
                    if(itMigratedList != migrated_list.end()) {
                        totalPageWriteToStorageWithPingpong++;
                        PRINTV(logfile << "migrated_list contains key: " << *itLRU_t2 << "** totalPageWriteToStorageWithPingpong size: "<< totalPageWriteToStorageWithPingpong <<endl;);
                        migrated_list.remove(*itLRU_t2);
                        PRINTV(logfile << "migrated_list removes key: " << *itLRU_t2 << "** migrated_list size: "<< migrated_list.size() <<endl;);
                    }
                }
            }
        }
    }
    ///*************************************************************************************************
    ///subroutine evcit a real page from t1 starting from the LRU position of t1_key
    void evictRealLRUPageFromT1() {
        typename key_tracker_type::iterator itLRU = t1_key.begin();
        assert(itLRU != t1_key.end());
        typename key_to_value_type::iterator it_t1a = t1a.find(*itLRU);
        typename key_to_value_type::iterator it_t1b = t1b.find(*itLRU);

        while(itLRU != t1_key.end()) {
            itLRU = t1_key.begin();
            it_t1a = t1a.find(*itLRU);
            it_t1b = t1b.find(*itLRU);
            ///the LRU page at t1a
            if(it_t1a != t1a.end()) {
                assert(it_t1b == t1b.end());
                ///ziqi: if PCR of the page at LRU position of lru list for read is 1, it is a ghost, evcit it and continue
                if((it_t1a->second.first.getReq().flags) & PCR) {
                    t1a.erase(it_t1a);
                    t1_key.remove(*itLRU);
                    PRINTV(logfile << "evictRealLRUPageFromT1 evitcs a PCR from t1a: " << *itLRU << "** t1a size: "<< t1a.size()<<", realPageAtDRAM size: "<<realPageAtDRAM<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                }
                ///ziqi: if PCR of the page at LRU position of lru list for read is 0, it is a read page, evcit it and break
                else {
                    t1a.erase(it_t1a);
                    t1_key.remove(*itLRU);
                    realPageAtDRAM--;

                    ///if the LRU page of t1 exists in migrated_list, evcit it
                    typename migrated_list_type::iterator itMigratedList = find(migrated_list.begin(), migrated_list.end(), *itLRU);
                    if(itMigratedList != migrated_list.end()) {
                        migrated_list.remove(*itLRU);
                        PRINTV(logfile << "migrated_list removes key: " << *itLRU << "** migrated_list size: "<< migrated_list.size() <<endl;);
                    }

                    PRINTV(logfile << "evictRealLRUPageFromT1 evitcs a real page from t1a: " << *itLRU << "** t1a size: "<< t1a.size()<<", realPageAtDRAM size: "<<realPageAtDRAM<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                    break;
                }
            }
            ///the LRU page at t1b
            if(it_t1b != t1b.end()) {
                ///make sure the evicted page from t1b is a real page
                assert( !(it_t1b->second.first.getReq().flags & PCR) );
                assert(it_t1a == t1a.end());
                t1b.erase(it_t1b);
                t1_key.remove(*itLRU);

                ///if the LRU page of t1 exists in migrated_list, evcit it
                typename migrated_list_type::iterator itMigratedList = find(migrated_list.begin(), migrated_list.end(), *itLRU);
                if(itMigratedList != migrated_list.end()) {
                    migrated_list.remove(*itLRU);
                    PRINTV(logfile << "migrated_list removes key: " << *itLRU << "** migrated_list size: "<< migrated_list.size() <<endl;);
                }

                PRINTV(logfile << "evictRealLRUPageFromT1 evitcs a real page from t1b: " << *itLRU << "** t1a size: "<< t1a.size()<<", realPageAtDRAM size: "<<realPageAtDRAM<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                break;
            }
        }
    }

private:

    // The function to be cached
    V(*_fn)(const K & , V);
    // Maximum number of key-value pairs to be retained
    const size_t _capacity;

    unsigned levelMinusMinus;

    ///ziqi: Key access history clean page LRU list
    key_tracker_type t1_key;
    ///ziqi: Key-to-value lookup clean page in DRAM
    key_to_value_type t1a;
    ///ziqi: Key-to-value lookup clean page in NVRAM
    key_to_value_type t1b;
    ///ziqi: Key access history dirty page LRU list
    key_tracker_type t2_key;
    ///ziqi: Key-to-value lookup dirty page in NVRAM
    key_to_value_type t2;
    ///ziqi: Key access history clean ghost LRU list
    key_tracker_type b1_key;
    ///ziqi: Key-to-value lookup clean ghost pages
    key_to_value_type b1;
    ///ziqi: Key access history dirty ghost LRU list
    key_tracker_type b2_key;
    ///ziqi: Key-to-value lookup dirty ghost pages
    key_to_value_type b2;

    ///ziqi: list of all the migrated pages from NVRAM to DRAM
    migrated_list_type migrated_list;
};

#endif //end hybrid
