//
// C++ Implementation: hybrid-fixed, dram used for read and nvram used for write
//
// Author: ziqi fan, UMN
//

#ifndef HybridFixed_H
#define HybridFixed_H

#include <map>
#include <list>
#include "cassert"
#include "iostream"
#include "iomanip"
#include "bitset"
#include "global.h"
#include "baseCache.h"

using namespace std;

extern int totalPageWriteToStorage;

extern int writeHitOnDirty;

extern int writeHitOnClean;

extern int dirtyPageInCache;

extern double priceDvsN;

extern double moneyAllo4D;

extern int nvramSize;

extern int readHitOnNVRAM;

extern int readHitOnDRAM;

// Class providing fixed-size (by number of records)
// LRU-replacement cache of a function with signature
// V f(K)
template <typename K, typename V>
class HybridFixed : public TestCache<K, V>
{
public:
    // Key access history, most recent at back
    typedef list<K> key_tracker_type;
    // Key to value and key history iterator
    typedef map
    < K, pair<V, typename key_tracker_type::iterator> > 	key_to_value_type;

    // Constuctor specifies the cached function and
    // the maximum number of records to be stored.
    HybridFixed(
        V(*f)(const K & , V),
        size_t c,
        unsigned levelMinus
    ) : _fn(f) , _capacity(c), levelMinusMinus(levelMinus)  {
    }
    // Obtain value of the cached function for k

    uint32_t access(const K &k  , V &value, uint32_t status) {
        size_t DRAM_capacity = (size_t)_capacity;
        int NVM_capacity = nvramSize;

        PRINTV(logfile << endl;);

        assert((t1a.size() + t1b.size() + t2.size()) <= ((unsigned)NVM_capacity+DRAM_capacity) );
        assert(t2.size() <= (unsigned)NVM_capacity);
        assert(t1b.size() <= (unsigned)NVM_capacity);
        assert(t1a.size() <= DRAM_capacity);
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
        //const typename key_to_value_type::iterator itNew	= _key_to_value.find(k);
        ///ziqi: Hybrid-fixed Case I: x hit in t2, then move x to MRU of t2
        if(it_t2 != t2.end()) {
            PRINTV(logfile << "Case I hit on t2: " << k << endl;);

            if(status & READ) {
                readHitOnNVRAM++;
            }

            t2.erase(it_t2);
            t2_key.remove(k);
            assert(t2.size() < DRAM_capacity);
            const V v = _fn(k, value);
            // Record k as most-recently-used key
            typename key_tracker_type::iterator itNew = t2_key.insert(t2_key.end(), k);
            // Create the key-value entry,
            // linked to the usage record.
            t2.insert(make_pair(k, make_pair(v, itNew)));
            PRINTV(logfile << "Case I insert key to t2: " << k << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<endl;);
            return (status | PAGEHIT | BLKHIT);
        }

        ///ziqi: Hybrid-fixed Case II: x hit in t1, then move x to t1 if it's a read, or to t2 if it's a write
        if((it_t1a != t1a.end()) || (it_t1b != t1b.end())) {
            assert(!((it_t1a != t1a.end()) && (it_t1b != t1b.end())));
            ///ziqi: if it is a write request
            if(status & WRITE) {
                if(it_t1a != t1a.end()) {
                    ///evict the hitted clean page from t1a
                    t1a.erase(it_t1a);
                    t1_key.remove(k);
                    const V v = _fn(k, value);
                    // NVRAM is not full
                    if(t2.size()+t1b.size() < (unsigned)NVM_capacity) {
                        PRINTV(logfile << "Case II write hit on t1a, and NVRAM is not full: " << k << endl;);
                        // Record k as most-recently-used key
                        assert(t2_key.size() < (unsigned)NVM_capacity);
                        typename key_tracker_type::iterator itNew
                            = t2_key.insert(t2_key.end(), k);
                        // Create the key-value entry,
                        // linked to the usage record.
                        assert(t2.size() < (unsigned)NVM_capacity);
                        t2.insert(make_pair(k, make_pair(v, itNew)));
                        PRINTV(logfile << "Case II insert dirty key to t2: " << k << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<endl;);
                    }
                    // NVRAM is full
                    else {
                        if(t2.size() == (unsigned)NVM_capacity) {
                            PRINTV(logfile << "Case II write hit on t1a, and NVRAM is full, t2.size() == NVRAM: " << k << endl;);
                            ///select the LRU page of t2
                            typename key_tracker_type::iterator itLRU = t2_key.begin();
                            assert(itLRU != t2_key.end());
                            typename key_to_value_type::iterator it = t2.find(*itLRU);
                            assert(it != t2.end());
                            ///insert the LRU page for t2 to t1a, since t1a has at least one page space left
                            ///Note that we insert the page from t2 to the LRU position of t1, it is NOT MRU position
                            typename key_tracker_type::iterator itNewTmp = t1_key.insert(t1_key.begin(), *itLRU);
                            // Create the key-value entry,
                            // linked to the usage record.
                            const V v_tmp = _fn(*itLRU, it->second.first);
                            assert(t1a.size() < DRAM_capacity);
                            t1a.insert(make_pair(*itLRU, make_pair(v_tmp, itNewTmp)));
                            PRINTV(logfile << "Case II insert key to t1a: " << *itLRU << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<endl;);

                            // flush back and evcit the LRU page of t2
                            t2.erase(it);
                            t2_key.remove(*itLRU);
                            PRINTV(logfile << "Case II (NVM is filled with dirty pages) evicting t2 and flushing back to DiskSim input trace " << *itLRU << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<endl;);
                            // DiskSim format Request_arrival_time Device_number Block_number Request_size Request_flags
                            // Device_number is set to 1. About Request_flags, 0 is for write and 1 is for read
                            PRINTV(logfile << "flushing to disksim input file" <<  endl;);
                            PRINTV(DISKSIMINPUTSTREAM<<setfill(' ')<<left<<fixed<<setw(25)<<v.getReq().issueTime<<left<<setw(8)<<"0"<<left<<fixed<<setw(12)<<*itLRU<<left<<fixed<<setw(8)<<"1"<<"0"<<endl;);
                            totalPageWriteToStorage++;

                            ///migrate the hitted page to MRU of t2
                            assert(t2.size()+t1b.size() < (unsigned)NVM_capacity);
                            // Record k as most-recently-used key
                            typename key_tracker_type::iterator itNew = t2_key.insert(t2_key.end(), k);
                            // Create the key-value entry,
                            // linked to the usage record.
                            t2.insert(make_pair(k, make_pair(v, itNew)));
                            PRINTV(logfile << "Case II insert dirty key to t2: " << k << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<endl;);
                        }
                        if(t2.size() < (unsigned)NVM_capacity) {
                            PRINTV(logfile << "Case II write hit on t1a, and NVRAM is full, t2.size() < NVRAM: " << k << endl;);
                            ///migrate a clean page from t1b to t1a
                            typename key_to_value_type::iterator it_t1b_tmp = t1b.begin();
                            assert(it_t1b_tmp != t1b.end());
                            typename key_tracker_type::iterator it_t1b_key_tmp = t1_key.begin();
                            assert(it_t1b_key_tmp != t1_key.end());
                            const V v_tmp = _fn(it_t1b_tmp->first, it_t1b_tmp->second.first);
                            t1a.insert(make_pair(it_t1b_tmp->first, make_pair(v_tmp, it_t1b_key_tmp)));
                            ///Note only t1b need to erase, t1_key is not need to evict and insert since the page's position in t1_key is not changed
                            t1b.erase(it_t1b_tmp);
                            PRINTV(logfile << "Case II migrate clean page from t1b to t1a: " << it_t1b_tmp->first << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<endl;);
                            ///migrate the hitted page to MRU of t2
                            assert(t2.size()+t1b.size() < (unsigned)NVM_capacity);
                            // Record k as most-recently-used key
                            typename key_tracker_type::iterator itNew = t2_key.insert(t2_key.end(), k);
                            // Create the key-value entry,
                            // linked to the usage record.
                            t2.insert(make_pair(k, make_pair(v, itNew)));
                            PRINTV(logfile << "Case II insert dirty key to t2: " << k << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<endl;);
                        }
                    }

                }
                if(it_t1b != t1b.end()) {
                    PRINTV(logfile << "Case II write hit on t1b " << k << endl;);
                    ///evict the hitted clean from t1b
                    t1b.erase(it_t1b);
                    t1_key.remove(k);
                    const V v = _fn(k, value);
                    // Record k as most-recently-used key
                    typename key_tracker_type::iterator itNew
                        = t2_key.insert(t2_key.end(), k);
                    // Create the key-value entry,
                    // linked to the usage record.
                    assert(t2.size() < (unsigned)NVM_capacity);
                    t2.insert(make_pair(k, make_pair(v, itNew)));
                    PRINTV(logfile << "Case II insert dirty key to t2: " << k << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<endl;);
                }
            }
            ///ziqi: if it is a read request
            else {
                if(it_t1a != t1a.end()) {
                    PRINTV(logfile << "Case II Read hit on t1a " << k << endl;);

                    readHitOnDRAM++;

                    t1a.erase(it_t1a);
                    t1_key.remove(k);
                    const V v = _fn(k, value);
                    // Record k as most-recently-used key
                    typename key_tracker_type::iterator itNew
                        = t1_key.insert(t1_key.end(), k);
                    // Create the key-value entry,
                    // linked to the usage record.
                    assert(t1a.size() < DRAM_capacity);
                    t1a.insert(make_pair(k, make_pair(v, itNew)));
                    PRINTV(logfile << "Case II insert clean key to t1a: " << k << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<endl;);
                }
                if(it_t1b != t1b.end()) {
                    PRINTV(logfile << "Case II Read hit on t1b " << k << endl;);

                    readHitOnNVRAM++;

                    t1b.erase(it_t1b);
                    t1_key.remove(k);
                    const V v = _fn(k, value);
                    // Record k as most-recently-used key
                    typename key_tracker_type::iterator itNew
                        = t1_key.insert(t1_key.end(), k);
                    // Create the key-value entry,
                    // linked to the usage record.
                    assert(t1b.size() + t2.size() < (unsigned)NVM_capacity);
                    t1b.insert(make_pair(k, make_pair(v, itNew)));
                    PRINTV(logfile << "Case II insert clean key to t1b: " << k << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<endl;);
                }
            }
            return (status | PAGEHIT | BLKHIT);
        }

        ///ziqi: HYBRID-fixed Case III: x is cache miss
        else {
            PRINTV(logfile << "Case III miss on key: " << k << endl;);
            const V v = _fn(k, value);
            ///write request
            if(status & WRITE) {
                ///NVM is not full
                if(t1b.size() + t2.size() < (unsigned)NVM_capacity) {
                    // Record k as most-recently-used key
                    typename key_tracker_type::iterator itNew = t2_key.insert(t2_key.end(), k);
                    // Create the key-value entry,
                    // linked to the usage record.
                    assert(t1b.size() + t2.size() < (unsigned)NVM_capacity);
                    t2.insert(make_pair(k, make_pair(v, itNew)));
                    PRINTV(logfile << "Case III insert dirty key to t2: " << k << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<endl;);
                }
                ///NVM is full
                else {
                    ///if t1b is empty
                    ///insert the LRU page of t2 to t1a if DRAM is not full
                    ///and flush and evict the LRU page of t2
                    ///finally, move x to MRU of t2
                    if(t2.size() == (unsigned)NVM_capacity) {
                        ///select the LRU page of t2
                        typename key_tracker_type::iterator itLRU = t2_key.begin();
                        assert(itLRU != t2_key.end());
                        typename key_to_value_type::iterator itLRUValue = t2.find(*itLRU);
                        assert(itLRUValue != t2.end());
                        ///if t1a is not full, insert the LRU page of t2 to t1a's LRU position NOT MRU position
                        if(t1a.size() < DRAM_capacity) {
                            typename key_tracker_type::iterator itNewTmp = t1_key.insert(t1_key.begin(), *itLRU);
                            // Create the key-value entry,
                            // linked to the usage record.
                            const V v_tmp = _fn(*itLRU, itLRUValue->second.first);
                            assert(t1a.size() < DRAM_capacity);
                            t1a.insert(make_pair(*itLRU, make_pair(v_tmp, itNewTmp)));
                            PRINTV(logfile << "Case III insert key to t1a: " << *itLRU << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<endl;);
                        }
                        ///flush and evict the LRU page of t2
                        // Erase both elements to completely purge record
                        t2.erase(itLRUValue);
                        t2_key.remove(*itLRU);
                        // DiskSim format Request_arrival_time Device_number Block_number Request_size Request_flags
                        // Device_number is set to 1. About Request_flags, 0 is for write and 1 is for read
                        PRINTV(logfile << "flushing to disksim input file" <<  endl;);
                        PRINTV(DISKSIMINPUTSTREAM<<setfill(' ')<<left<<fixed<<setw(25)<<v.getReq().issueTime<<left<<setw(8)<<"0"<<left<<fixed<<setw(12)<<*itLRU<<left<<fixed<<setw(8)<<"1"<<"0"<<endl;);
                        totalPageWriteToStorage++;

                        PRINTV(logfile << "Case III (NVM is filled with dirty pages) evicting t2 " << *itLRU << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<endl;);

                        ///insert the missed page to MRU of t2
                        // Record k as most-recently-used key
                        typename key_tracker_type::iterator itNew = t2_key.insert(t2_key.end(), k);
                        // Create the key-value entry,
                        // linked to the usage record.
                        assert(t2.size()+t1b.size() < (unsigned)NVM_capacity);
                        t2.insert(make_pair(k, make_pair(v, itNew)));
                        PRINTV(logfile << "Case III insert dirty key to t2: " << k << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<endl;);
                    }
                    ///t1b is not empty
                    else {
                        ///DRAM is not full
                        if(t1a.size() < DRAM_capacity) {
                            ///migrate a clean page from t1b to t1a
                            typename key_to_value_type::iterator it_t1b_tmp = t1b.begin();
                            assert(it_t1b_tmp != t1b.end());
                            typename key_tracker_type::iterator it_t1b_key_tmp = t1_key.begin();
                            assert(it_t1b_key_tmp != t1_key.end());
                            const V v_tmp = _fn(it_t1b_tmp->first, it_t1b_tmp->second.first);
                            assert(t1a.size() < DRAM_capacity);
                            t1a.insert(make_pair(it_t1b_tmp->first, make_pair(v_tmp, it_t1b_key_tmp)));
                            t1b.erase(it_t1b_tmp);
                            PRINTV(logfile << "Case III migrate clean page from t1b to t1a: " << it_t1b_tmp->first << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<endl;);
                            ///insert the missed page to MRU of t2
                            typename key_tracker_type::iterator itNew = t2_key.insert(t2_key.end(), k);
                            // Create the key-value entry,
                            // linked to the usage record.
                            assert(t2.size()+t1b.size() < (unsigned)NVM_capacity);
                            t2.insert(make_pair(k, make_pair(v, itNew)));
                            PRINTV(logfile << "Case III insert dirty key to t2: " << k << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<endl;);
                        }
                        ///DRAM is full
                        else {
                            ///find the LRU page of t1
                            typename key_tracker_type::iterator itLRU = t1_key.begin();
                            assert(itLRU != t1_key.end());
                            typename key_to_value_type::iterator it_tmp_t1a = t1a.find(*itLRU);
                            typename key_to_value_type::iterator it_tmp_t1b = t1b.find(*itLRU);
                            assert(it_tmp_t1a != t1a.end() || it_tmp_t1b != t1b.end());
                            assert( ! (it_tmp_t1a != t1a.end() && it_tmp_t1b != t1b.end()));
                            if(it_tmp_t1a != t1a.end()) {
                                ///evict the LRU page from t1a
                                t1a.erase(it_tmp_t1a);
                                t1_key.remove(*itLRU);
                                PRINTV(logfile << "Case III evict the LRU page from t1a: " << *itLRU << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<endl;);
                                ///migrate a clean page from t1b to t1a
                                typename key_to_value_type::iterator it_t1b_tmp = t1b.begin();
                                assert(it_t1b_tmp != t1b.end());
                                typename key_tracker_type::iterator it_t1b_key_tmp = t1_key.begin();
                                assert(it_t1b_key_tmp != t1_key.end());
                                const V v_tmp = _fn(it_t1b_tmp->first, it_t1b_tmp->second.first);
                                assert(t1a.size() < DRAM_capacity);
                                t1a.insert(make_pair(it_t1b_tmp->first, make_pair(v_tmp, it_t1b_key_tmp)));
                                t1b.erase(it_t1b_tmp);
                                PRINTV(logfile << "Case III migrate clean page from t1b to t1a: " << it_t1b_tmp->first << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<endl;);
                            }
                            if(it_tmp_t1b != t1b.end()) {
                                ///evict the LRU page from t1b
                                t1b.erase(it_tmp_t1b);
                                t1_key.remove(*itLRU);
                                PRINTV(logfile << "Case III evict the LRU page from t1b: " << *itLRU << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<endl;);
                            }
                            ///insert the missed page to MRU of t2
                            typename key_tracker_type::iterator itNew = t2_key.insert(t2_key.end(), k);
                            // Create the key-value entry,
                            // linked to the usage record.
                            assert(t2.size()+t1b.size() < (unsigned)NVM_capacity);
                            t2.insert(make_pair(k, make_pair(v, itNew)));
                            PRINTV(logfile << "Case III insert dirty key to t2: " << k << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<endl;);
                        }
                    }
                }
            }
            ///read request
            else {
                ///DRAM is not full
                if(t1a.size() < DRAM_capacity) {
                    ///insert the missed page to MRU of t1 in t1a
                    // Record k as most-recently-used key
                    typename key_tracker_type::iterator itNew = t1_key.insert(t1_key.end(), k);
                    assert(t1a.size() < DRAM_capacity);
                    t1a.insert(make_pair(k, make_pair(v, itNew)));
                    PRINTV(logfile << "Case III insert key to t1a: " << k << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<endl;);
                }
                ///DRAM is full
                else {
                    ///NVRAM is not full
                    if(t1b.size() + t2.size() < (unsigned)NVM_capacity) {
                        ///insert the missed page to MRU of t1 in t1b
                        typename key_tracker_type::iterator itNew = t1_key.insert(t1_key.end(), k);
                        assert(t1b.size() + t2.size() < (unsigned)NVM_capacity);
                        t1b.insert(make_pair(k, make_pair(v, itNew)));
                        PRINTV(logfile << "Case III insert key to t1b: " << k << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<endl;);
                    }
                    ///NVRAM is full
                    else {
                        ///NVRAM is filled with dirty pages
                        if(t2.size() == (unsigned)NVM_capacity) {
                            ///evict the LRU page of t1 in t1a
                            typename key_tracker_type::iterator itLRU = t1_key.begin();
                            assert(itLRU != t1_key.end());
                            typename key_to_value_type::iterator it_tmp_t1a = t1a.find(*itLRU);
                            assert(it_tmp_t1a != t1a.end());
                            ///evict the LRU page from t1a
                            t1a.erase(it_tmp_t1a);
                            t1_key.remove(*itLRU);
                            PRINTV(logfile << "Case III evict the LRU page from t1a: " << *itLRU << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<endl;);
                            ///insert the missed page to MRU of t1 in t1a
                            typename key_tracker_type::iterator itNew = t1_key.insert(t1_key.end(), k);
                            assert(t1a.size() < DRAM_capacity);
                            t1a.insert(make_pair(k, make_pair(v, itNew)));
                            PRINTV(logfile << "Case III insert key to t1a: " << k << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<endl;);
                        }
                        ///NVRAM has some clean pages in t1b
                        else {
                            ///find the LRU page of t1
                            typename key_tracker_type::iterator itLRU = t1_key.begin();
                            assert(itLRU != t1_key.end());
                            typename key_to_value_type::iterator it_tmp_t1a = t1a.find(*itLRU);
                            typename key_to_value_type::iterator it_tmp_t1b = t1b.find(*itLRU);
                            assert(it_tmp_t1a != t1a.end() || it_tmp_t1b != t1b.end());
                            assert( ! (it_tmp_t1a != t1a.end() && it_tmp_t1b != t1b.end()));
                            if(it_tmp_t1a != t1a.end()) {
                                ///evict the LRU page from t1a
                                t1a.erase(it_tmp_t1a);
                                t1_key.remove(*itLRU);
                                PRINTV(logfile << "Case III evict the LRU page from t1a: " << *itLRU << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<endl;);
                                ///insert the missed page to MRU of t1 in t1a
                                typename key_tracker_type::iterator itNew = t1_key.insert(t1_key.end(), k);
                                assert(t1a.size() < DRAM_capacity);
                                t1a.insert(make_pair(k, make_pair(v, itNew)));
                                PRINTV(logfile << "Case III insert key to t1a: " << k << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<endl;);
                            }
                            if(it_tmp_t1b != t1b.end()) {
                                ///evict the LRU page from t1b
                                t1b.erase(it_tmp_t1b);
                                t1_key.remove(*itLRU);
                                PRINTV(logfile << "Case III evict the LRU page from t1b: " << *itLRU << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<endl;);
                                ///insert the missed page to MRU of t1 in t1b
                                typename key_tracker_type::iterator itNew = t1_key.insert(t1_key.end(), k);
                                assert(t1b.size() + t2.size() < (unsigned)NVM_capacity);
                                t1b.insert(make_pair(k, make_pair(v, itNew)));
                                PRINTV(logfile << "Case III insert key to t1b: " << k << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<endl;);
                            }
                        }
                    }
                }
            }
            return (status | PAGEMISS);
        }
        ///should never reach here
        assert(0);
        return 0;
    } //end operator access

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
};

#endif //end hybrid
