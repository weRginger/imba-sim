//
// C++ Implementation: Latest Hibachi algorithm combining idea with I/O-Cache
//
// Author: Ziqi Fan, UMN
//

#ifndef Hibachi_H
#define Hibachi_H

#include <map>
#include <list>
#include "cassert"
#include "iostream"
#include "iomanip"
#include "bitset"
#include "global.h"
#include "baseCache.h"

using namespace std;

// when evict from dirty cache side, if the maxLength in seqList is smaller than threshold, evcit according to LRU order
extern int threshold;

extern int totalPageWriteToStorage;

extern int nvramSize;

extern int readHitOnNVRAM;

extern int readHitOnDRAM;

// Class providing fixed-size (by number of records)
// LRU-replacement cache of a function with signature
// V f(K)
template <typename K, typename V>
class Hibachi : public TestCache<K, V>
{
public:
    // Key access history, most recent at back
    typedef list<K> key_tracker_type;
    // Key to value and key history iterator
    typedef map<K, pair<V, typename key_tracker_type::iterator>> key_to_value_type;

    typedef map<long long, int> key_to_value_type_frequencyList;

    typedef map<int, int> key_to_value_type_seqList;

    // Constuctor specifies the cached function and
    // the maximum number of records to be stored.
    Hibachi(
        V(*f)(const K & , V),
        size_t c,
        unsigned levelMinus
    ) : _fn(f) , _capacity(c), levelMinusMinus(levelMinus)  {
    }
    // Obtain value of the cached function for k

    uint32_t access(const K &k  , V &value, uint32_t status) {
        size_t DRAM_capacity = (size_t)_capacity;
        int NVM_capacity = nvramSize;
        PRINTV(logfile<<"NVM_capacity = "<<nvramSize<<endl;);

        PRINTV(logfile << endl;);
        // p denotes the length of t1 and (_capacity - p) denotes the lenght of t2
        static int p=0;

        assert((t1a.size() + t1b.size() + t2.size()) <= ((unsigned)NVM_capacity+DRAM_capacity));
        assert((t1a.size() + t1b.size() + t2.size() + b1.size() + b2.size()) <= 2*((unsigned)NVM_capacity+DRAM_capacity));
        assert(t2.size() <= (unsigned)NVM_capacity);
        assert(t1b.size() <= (unsigned)NVM_capacity);
        assert(t1a.size() <= DRAM_capacity);
        assert((t2.size() + b2.size()) <= 2*((unsigned)NVM_capacity+DRAM_capacity));
        assert(_capacity != 0);

        PRINTV(logfile << "Access key: " << k << endl;);

        while ((t1a.size() + t1b.size() + b1.size()) > ((unsigned)NVM_capacity+DRAM_capacity)) {
            // find the least frequency in fList
            // store the least frequency in fList
            int leastFrequency_tmp = 9999999;
            // store the key of least frequency in fList
            long long leastFrequencyKey_tmp = 0;

            // evcit LFU page of b1
            // check starting from MRU position of b1, select the least frequency one, if tie, select the one near LRU position
            typename key_tracker_type::iterator itMRU = b1_key.end();
            itMRU--;
            while(itMRU != b1_key.begin()) {
                typename key_to_value_type_frequencyList::iterator itfList = fList.find(*itMRU);
                assert(itfList != fList.end());
                if(itfList->second <= leastFrequency_tmp) {
                    leastFrequency_tmp = itfList->second;
                    leastFrequencyKey_tmp = itfList->first;
                }
                itMRU--;
            }

            itMRU = b1_key.begin();
            typename key_to_value_type_frequencyList::iterator itfList = fList.find(*itMRU);
            assert(itfList != fList.end() );
            if(itfList->second <= leastFrequency_tmp) {
                leastFrequency_tmp = itfList->second;
                leastFrequencyKey_tmp = itfList->first;
            }

            PRINTV(logfile << "leastFrequencyKey_tmp: " << leastFrequencyKey_tmp << ", leastFrequency_tmp: " << leastFrequency_tmp << endl;);

            typename key_to_value_type::iterator it_b1_tmp	= b1.find(leastFrequencyKey_tmp);
            assert(it_b1_tmp != b1.end());
            b1.erase(it_b1_tmp);
            b1_key.remove(leastFrequencyKey_tmp);
            // evict LFU page of b1 from fList
            fList.erase(leastFrequencyKey_tmp);

            PRINTV(logfile << "Make sure (t1a.size() + t1b.size() + b1.size()) <= _capacity evicts LFU page from b1: " << leastFrequencyKey_tmp << "++frequency: " << fList.find(leastFrequencyKey_tmp)->second << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
        }

        assert((t1a.size() + t1b.size() + b1.size()) <= ((unsigned)NVM_capacity+DRAM_capacity));

        // Attempt to find existing record
        const typename key_to_value_type::iterator it_t1a	= t1a.find(k);
        const typename key_to_value_type::iterator it_t1b	= t1b.find(k);
        const typename key_to_value_type::iterator it_t2	= t2.find(k);
        const typename key_to_value_type::iterator it_b1	= b1.find(k);
        const typename key_to_value_type::iterator it_b2	= b2.find(k);

        ///*************************************************************************************************
        // Hibachi Case I: x hit in t2
        if(it_t2 != t2.end()) {
            // if it is a write request, move x to MRU of t2 w/o frequency increasing
            if(status & WRITE) {
                PRINTV(logfile << "Case I write hit on t2 with key: " << k <<endl;);
                t2.erase(it_t2);
                t2_key.remove(k);
                assert(t2.size() < ((unsigned)NVM_capacity+DRAM_capacity));
                const V v = _fn(k, value);
                // Record k as most-recently-used key
                typename key_tracker_type::iterator itNew = t2_key.insert(t2_key.end(), k);
                // Create the key-value entry,
                // linked to the usage record.
                assert(t1b.size() + t2.size() < (unsigned)NVM_capacity);
                t2.insert(make_pair(k, make_pair(v, itNew)));
                PRINTV(logfile << "Case I write hit move to MRU of t2: " << k << "++frequency: " << fList.find(k)->second << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
            }
            // if it is a read request, only increase frequency
            else {
                PRINTV(logfile << "Case I read hit on t2 with key: " << k <<endl;);

                readHitOnNVRAM++;

                typename key_to_value_type_frequencyList::iterator itfList = fList.find(k);
                assert(itfList != fList.end() );
                if(itfList != fList.end()) {
                    itfList->second = itfList->second+1;
                }

                PRINTV(logfile << "Case I read hit increase frequency: " << k << "++frequency: " << fList.find(k)->second << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
            }
            return (status | PAGEHIT | BLKHIT);
        }
        ///*************************************************************************************************
        // Hibachi Case II: x hit in t1
        else if((it_t1a != t1a.end()) || (it_t1b != t1b.end())) {
            assert(!((it_t1a != t1a.end()) && (it_t1b != t1b.end())));
            ///ziqi: if it is a write request
            if(status & WRITE) {
                if(it_t1a != t1a.end()) {
                    PRINTV(logfile << "Case II hit on t1a: " << k << endl;);
                    ///evict from t1a
                    t1a.erase(it_t1a);
                    t1_key.remove(k);

                    // NVRAM is not full
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

                        seqListInsert(k);

                        PRINTV(logfile << "Case II insert dirty key to t2: " << k << "++frequency: " << fList.find(k)->second << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                    }
                    // NVRAM is full
                    else {
                        ///NVRAM is filled with dirty pages
                        if(t2.size() == (unsigned)NVM_capacity) {
                            PRINTV(logfile << "Case II Write hit on t1a, and NVRAM is full, t2.size() == NVRAM: "<< endl;);

                            ///select the LRU page of t2
                            typename key_tracker_type::iterator itLRU = t2_key.begin();
                            assert(itLRU != t2_key.end());
                            typename key_to_value_type::iterator it = t2.find(*itLRU);
                            assert(it != t2.end());
                            ///insert LRU page of t2 to LRU of t1a
                            typename key_tracker_type::iterator itNewTmp = t1_key.insert(t1_key.begin(), *itLRU);
                            // Create the key-value entry,
                            // linked to the usage record.
                            const V v_tmp = _fn(*itLRU, it->second.first);
                            assert(t1a.size() < DRAM_capacity);
                            t1a.insert(make_pair(*itLRU, make_pair(v_tmp, itNewTmp)));
                            ///evict and flush LRU page of t2
                            t2.erase(it);
                            t2_key.remove(*itLRU);

                            // DiskSim format Request_arrival_time Device_number Block_number Request_size Request_flags
                            // Device_number is set to 1. About Request_flags, 0 is for write and 1 is for read
                            PRINTV(logfile << "flushing to disksim input file" <<  endl;);
                            PRINTV(DISKSIMINPUTSTREAM<<setfill(' ')<<left<<fixed<<setw(25)<<value.getReq().issueTime<<left<<setw(8)<<"0"<<left<<fixed<<setw(12)<<*itLRU<<left<<fixed<<setw(8)<<"1"<<"0"<<endl;);

                            seqListEvict(*itLRU);

                            PRINTV(logfile << "Case II (NVRAM is filled with dirty pages) evicting t2 and flushing back " << *itLRU << "++frequency: " << fList.find(*itLRU)->second << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                            totalPageWriteToStorage++;
                        }
                        ///NVRAM is full but is not filled with dirty pages
                        else {
                            PRINTV(logfile << "Case II Write hit on t1a, and NVRAM is full, t2.size() < NVRAM: "<< endl;);
                            ///migrate a clean page from t1b to t1a
                            typename key_to_value_type::iterator it_t1b_tmp = t1b.begin();
                            assert(it_t1b_tmp != t1b.end());
                            typename key_tracker_type::iterator it_t1b_key_tmp = t1_key.end();
                            const V v_tmp = _fn(it_t1b_tmp->first, it_t1b_tmp->second.first);
                            assert(t1a.size() < DRAM_capacity);
                            t1a.insert(make_pair(it_t1b_tmp->first, make_pair(v_tmp, it_t1b_key_tmp)));

                            t1b.erase(it_t1b_tmp);
                            PRINTV(logfile << "Case II migrate clean page from t1b to t1a: " << it_t1b_tmp->first << "++frequency: " << fList.find(it_t1b_tmp->first)->second << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                        }
                        // finally
                        // migrate the hitted page to MRU of t2
                        // Record k as most-recently-used key
                        typename key_tracker_type::iterator itNew = t2_key.insert(t2_key.end(), k);
                        // Create the key-value entry,
                        // linked to the usage record.
                        assert(t2.size()+t1b.size() < (unsigned)NVM_capacity);
                        const V v = _fn(k, value);
                        t2.insert(make_pair(k, make_pair(v, itNew)));

                        seqListInsert(k);

                        PRINTV(logfile << "Case II insert dirty key to t2: " << k << "** t1a size: "<< t1a.size()<<", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                    }
                }
                if(it_t1b != t1b.end()) {
                    PRINTV(logfile << "Case II Write hit on t1b " << k << endl;);
                    // evict from t1b
                    t1b.erase(it_t1b);
                    t1_key.remove(k);

                    const V v_2 = _fn(k, value);
                    // Record k as most-recently-used key
                    typename key_tracker_type::iterator itNew = t2_key.insert(t2_key.end(), k);
                    // Create the key-value entry,
                    // linked to the usage record.
                    assert(t2.size()+t1b.size() < (unsigned)NVM_capacity);
                    t2.insert(make_pair(k, make_pair(v_2, itNew)));

                    seqListInsert(k);

                    PRINTV(logfile << "Case II insert dirty key to t2: " << k << "++frequency: " << fList.find(k)->second << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                }
            }
            // hit on t1a or t1b, and it is a read request
            else {
                if(it_t1a != t1a.end()) {
                    PRINTV(logfile << "Case II Read hit on t1a " << k << endl;);

                    readHitOnDRAM++;

                    ///increase frequency by 1
                    typename key_to_value_type_frequencyList::iterator itfList = fList.find(k);
                    assert(itfList != fList.end() );
                    if(itfList != fList.end()) {
                        itfList->second = itfList->second+1;
                    }

                    ///move the hit page to MRU of t1
                    t1a.erase(it_t1a);
                    t1_key.remove(k);

                    const V v = _fn(k, value);
                    // Record k as most-recently-used key
                    typename key_tracker_type::iterator itNew = t1_key.insert(t1_key.end(), k);
                    // Create the key-value entry,
                    // linked to the usage record.
                    assert(t1a.size() < DRAM_capacity);
                    t1a.insert(make_pair(k, make_pair(v, itNew)));

                    PRINTV(logfile << "Case II insert clean key to t1a: " << k << "++frequency: " << fList.find(k)->second << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                }
                if(it_t1b != t1b.end()) {
                    PRINTV(logfile << "Case II Read hit on t1b " << k << endl;);

                    readHitOnNVRAM++;

                    ///increase frequency by 1
                    typename key_to_value_type_frequencyList::iterator itfList = fList.find(k);
                    assert(itfList != fList.end() );
                    if(itfList != fList.end()) {
                        itfList->second = itfList->second+1;
                    }

                    ///move the hit page to MRU of t1
                    t1b.erase(it_t1b);
                    t1_key.remove(k);

                    const V v = _fn(k, value);
                    // Record k as most-recently-used key
                    typename key_tracker_type::iterator itNew = t1_key.insert(t1_key.end(), k);
                    // Create the key-value entry,
                    // linked to the usage record.
                    assert(t2.size()+t1b.size() < (unsigned)NVM_capacity);
                    t1b.insert(make_pair(k, make_pair(v, itNew)));

                    PRINTV(logfile << "Case II insert clean key to t1b: " << k << "++frequency: " << fList.find(k)->second << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                }
            }
            return (status | PAGEHIT | BLKHIT);
        }
        ///*************************************************************************************************
        // Hibachi Case III: x hit in b1, then enlarge t1, and move x from b1 to t1 if it's a read or to t2 if it's a write
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

            if((p+delta) > int(((unsigned)NVM_capacity+DRAM_capacity)))
                p = ((unsigned)NVM_capacity+DRAM_capacity);
            else
                p = p+delta;

            PRINTV(logfile << p << endl;);

            const V v = _fn(k, value);
            ///E&B
            REPLACE(k, v, p, status);

            b1.erase(it_b1);
            b1_key.remove(k);
            PRINTV(logfile << "Case III evicts b1 " << k <<  endl;);

            // if it is a write request
            if(status & WRITE) {
                PRINTV(logfile << "Case III write Hit on b1: " << k << endl;);
                // E&B function must have cleaned a space in t1a instead of t1b, so we need to move a page from t1b to t1a
                if(t2.size()+t1b.size() == (unsigned)NVM_capacity) {
                    typename key_to_value_type::iterator it_t1b_tmp = t1b.begin();
                    assert(it_t1b_tmp != t1b.end());

                    typename key_tracker_type::iterator it_t1b_key_tmp = t1_key.end();
                    const V v_tmp = _fn(it_t1b_tmp->first, it_t1b_tmp->second.first);
                    assert(t1a.size() < DRAM_capacity);
                    t1a.insert(make_pair(it_t1b_tmp->first, make_pair(v_tmp, it_t1b_key_tmp)));

                    t1b.erase(it_t1b_tmp);
                    PRINTV(logfile << "Case III migrate clean page from t1b to t1a: " << it_t1b_tmp->first << "++frequency: " << fList.find(it_t1b_tmp->first)->second << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                }
                assert(t2.size()+t1b.size()<(unsigned)NVM_capacity);
                // Record k as most-recently-used key
                typename key_tracker_type::iterator itNew = t2_key.insert(t2_key.end(), k);
                // Create the key-value entry,
                // linked to the usage record.
                assert(t2.size()+t1b.size() < (unsigned)NVM_capacity);
                t2.insert(make_pair(k, make_pair(v, itNew)));

                seqListInsert(k);

                PRINTV(logfile << "Case III insert a dirty page to t2: " << k << "++frequency: " << fList.find(k)->second << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
            }
            // if it is a read request
            else {
                PRINTV(logfile << "Case III read Hit on b1: " << k << endl;);
                // if t1a has space left, always insert clean page to it first
                if(t1a.size()<DRAM_capacity) {
                    // increase frequency by 1
                    typename key_to_value_type_frequencyList::iterator itfList = fList.find(k);
                    assert(itfList != fList.end() );
                    if(itfList != fList.end()) {
                        itfList->second = itfList->second+1;
                    }

                    // Record k as most-recently-used key
                    typename key_tracker_type::iterator itNew = t1_key.insert(t1_key.end(), k);
                    // Create the key-value entry,
                    // linked to the usage record.
                    assert(t1a.size() < DRAM_capacity);
                    t1a.insert(make_pair(k, make_pair(v, itNew)));

                    PRINTV(logfile << "Case III insert a clean page to t1a: " << k << "++frequency: " << fList.find(k)->second << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                }
                // if t1a has no space left, use the space in NVRAM for clean, which is t1b
                else {
                    // increase frequency by 1
                    typename key_to_value_type_frequencyList::iterator itfList = fList.find(k);
                    assert(itfList != fList.end() );
                    if(itfList != fList.end()) {
                        itfList->second = itfList->second+1;
                    }

                    // Record k as most-recently-used key
                    typename key_tracker_type::iterator itNew = t1_key.insert(t1_key.end(), k);
                    // Create the key-value entry,
                    // linked to the usage record.
                    assert(t2.size()+t1b.size() < (unsigned)NVM_capacity);
                    t1b.insert(make_pair(k, make_pair(v, itNew)));
                    PRINTV(logfile << "Case III insert a clean page to t1b: " << k << "++frequency: " << fList.find(k)->second << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                }

                if(t1a.size() + t1b.size() + b1.size() > ((unsigned)NVM_capacity+DRAM_capacity)) {
                    if(b1.size() > 0) {
                        // find the least frequency in fList
                        // store the least frequency in fList
                        int leastFrequency = 9999999;
                        ///store the key of least frequency in fList
                        long long leastFrequencyKey = 0;

                        ///check starting from MRU position of b1, select the least frequency one, if tie, select the one near LRU position
                        typename key_tracker_type::iterator itMRU = b1_key.end();
                        itMRU--;
                        while(itMRU != b1_key.begin()) {
                            typename key_to_value_type_frequencyList::iterator itfList = fList.find(*itMRU);
                            assert(itfList != fList.end() );
                            if(itfList->second <= leastFrequency) {
                                leastFrequency = itfList->second;
                                leastFrequencyKey = itfList->first;
                            }
                            itMRU--;
                        }

                        itMRU = b1_key.begin();
                        typename key_to_value_type_frequencyList::iterator itfList = fList.find(*itMRU);
                        assert(itfList != fList.end() );
                        if(itfList->second <= leastFrequency) {
                            leastFrequency = itfList->second;
                            leastFrequencyKey = itfList->first;
                        }

                        PRINTV(logfile << "leastFrequencyKey: " << leastFrequencyKey << ", leastFrequency: " << leastFrequency << endl;);

                        // evcit LFU page of b1
                        typename key_to_value_type::iterator it_b1_tmp	= b1.find(leastFrequencyKey);
                        assert(it_b1_tmp != b1.end() );
                        b1.erase(it_b1_tmp);
                        b1_key.remove(leastFrequencyKey);
                        // evict LFU page of b1 from fList
                        fList.erase(leastFrequencyKey);

                        PRINTV(logfile << "Case III evicts LFU page from b1: " << leastFrequencyKey << "++frequency: " << fList.find(leastFrequencyKey)->second << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                    }
                    else {
                        // find the least frequency in fList
                        // store the least frequency in fList
                        int leastFrequency = 9999999;
                        // store the key of least frequency in fList
                        long long leastFrequencyKey = 0;

                        // check starting from MRU position of t1, select the least frequency one, if tie, select the one near LRU position
                        typename key_tracker_type::iterator itMRU = t1_key.end();
                        itMRU--;
                        while(itMRU != t1_key.begin()) {
                            typename key_to_value_type_frequencyList::iterator itfList = fList.find(*itMRU);
                            if(itfList->second <= leastFrequency) {
                                leastFrequency = itfList->second;
                                leastFrequencyKey = itfList->first;
                            }
                            itMRU--;
                        }

                        itMRU = t1_key.begin();
                        typename key_to_value_type_frequencyList::iterator itfList = fList.find(*itMRU);
                        assert(itfList != fList.end() );
                        if(itfList->second <= leastFrequency) {
                            leastFrequency = itfList->second;
                            leastFrequencyKey = itfList->first;
                        }

                        PRINTV(logfile << "leastFrequencyKey: " << leastFrequencyKey << ", leastFrequency: " << leastFrequency << endl;);

                        typename key_to_value_type::iterator it_t1a_tmp	= t1a.find(leastFrequencyKey);
                        typename key_to_value_type::iterator it_t1b_tmp	= t1b.find(leastFrequencyKey);

                        if(it_t1a_tmp != t1a.end()) {
                            assert(it_t1b_tmp == t1b.end());
                            // evcit LFU page of t1a
                            t1a.erase(it_t1a_tmp);
                            t1_key.remove(leastFrequencyKey);
                            // evict LFU page of b1 from fList
                            fList.erase(leastFrequencyKey);

                            PRINTV(logfile << "Case III evicts LFU page from t1a: " << leastFrequencyKey << "++frequency: " << fList.find(leastFrequencyKey)->second<< "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                        }

                        if(it_t1b_tmp != t1b.end()) {
                            assert(it_t1a_tmp == t1a.end());
                            // evcit LFU page of t1b
                            t1b.erase(it_t1b_tmp);
                            t1_key.remove(leastFrequencyKey);
                            // evict LFU page of b1 from fList
                            fList.erase(leastFrequencyKey);

                            PRINTV(logfile << "Case III evicts LFU page from t1b: " << leastFrequencyKey << "++frequency: " << fList.find(leastFrequencyKey)->second << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                        }
                    }
                }
            }
            return (status | PAGEMISS);
        }
        ///*************************************************************************************************
        // Hibachi Case IV: x hit in b2, then enlarge t2, and move x from b2 to t1 if it's a read or to t2 if it's a write
        else if(it_b2 != b2.end()) {
            // delta denotes the step in each ADAPTATION
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

            // E&B
            REPLACE(k, v, p, status);

            b2.erase(it_b2);
            b2_key.remove(k);
            PRINTV(logfile << "Case IV evicts b2 " << k <<  endl;);

            // if it is a write request
            if(status & WRITE) {
                PRINTV(logfile << "Case IV write Hit on b2: " << k << endl;);
                // Record k as most-recently-used key
                // E&B function must have cleaned a space in t1a instead of t1b, so we need to move a page from t1b to t1a
                if(t2.size()+t1b.size() == (unsigned)NVM_capacity) {
                    typename key_to_value_type::iterator it_t1b_tmp = t1b.begin();
                    assert(it_t1b_tmp != t1b.end());

                    typename key_tracker_type::iterator it_t1b_key_tmp = t1_key.end();
                    const V v_tmp = _fn(it_t1b_tmp->first, it_t1b_tmp->second.first);
                    assert(t1a.size() < DRAM_capacity);
                    t1a.insert(make_pair(it_t1b_tmp->first, make_pair(v_tmp, it_t1b_key_tmp)));

                    t1b.erase(it_t1b_tmp);
                    PRINTV(logfile << "Case IV migrate a clean page from t1b to t1a: " << it_t1b_tmp->first << "++frequency: " << fList.find(it_t1b_tmp->first)->second << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                }
                typename key_tracker_type::iterator itNew = t2_key.insert(t2_key.end(), k);
                // Create the key-value entry,
                // linked to the usage record.
                assert(t2.size()+t1b.size() < (unsigned)NVM_capacity);
                t2.insert(make_pair(k, make_pair(v, itNew)));

                seqListInsert(k);

                PRINTV(logfile << "Case IV insert a dirty page to t2: " << k << "++frequency: " << fList.find(k)->second << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
            }
            // if it is a read request
            else {
                PRINTV(logfile << "Case IV read Hit on b2: " << k << endl;);
                // if t1a has space left, always insert clean page to it first
                if(t1a.size()<DRAM_capacity) {
                    // increase frequency by 1
                    typename key_to_value_type_frequencyList::iterator itfList = fList.find(k);
                    if(itfList != fList.end()) {
                        itfList->second = itfList->second+1;
                    }

                    // Record k as most-recently-used key
                    typename key_tracker_type::iterator itNew = t1_key.insert(t1_key.end(), k);
                    // Create the key-value entry,
                    // linked to the usage record.
                    assert(t1a.size() < DRAM_capacity);

                    t1a.insert(make_pair(k, make_pair(v, itNew)));
                    PRINTV(logfile << "Case IV insert key to t1a: " << k << "++frequency: " << fList.find(k)->second << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                }
                // if t1a has no space left, use the space in NVRAM for clean, which is t1b
                else {
                    // increase frequency by 1
                    typename key_to_value_type_frequencyList::iterator itfList = fList.find(k);
                    assert(itfList != fList.end() );
                    if(itfList != fList.end()) {
                        itfList->second = itfList->second+1;
                    }

                    // Record k as most-recently-used key
                    typename key_tracker_type::iterator itNew = t1_key.insert(t1_key.end(), k);
                    // Create the key-value entry,
                    // linked to the usage record.
                    assert(t2.size()+t1b.size() < (unsigned)NVM_capacity);
                    t1b.insert(make_pair(k, make_pair(v, itNew)));
                    PRINTV(logfile << "Case IV insert key to t1b: " << k << "++frequency: " << fList.find(k)->second << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                }

                if(t1a.size() + t1b.size() + b1.size() > ((unsigned)NVM_capacity+DRAM_capacity)) {
                    if(b1.size() > 0) {
                        // find the least frequency in fList
                        // store the least frequency in fList
                        int leastFrequency = 9999999;
                        // store the key of least frequency in fList
                        long long leastFrequencyKey = 0;

                        // check starting from MRU position of b1, select the least frequency one, if tie, select the one near LRU position
                        typename key_tracker_type::iterator itMRU = b1_key.end();
                        itMRU--;
                        while(itMRU != b1_key.begin()) {
                            typename key_to_value_type_frequencyList::iterator itfList = fList.find(*itMRU);
                            assert(itfList != fList.end() );
                            if(itfList->second <= leastFrequency) {
                                leastFrequency = itfList->second;
                                leastFrequencyKey = itfList->first;
                            }
                            itMRU--;
                        }

                        itMRU = b1_key.begin();
                        typename key_to_value_type_frequencyList::iterator itfList = fList.find(*itMRU);
                        assert(itfList != fList.end() );
                        if(itfList->second <= leastFrequency) {
                            leastFrequency = itfList->second;
                            leastFrequencyKey = itfList->first;
                        }

                        PRINTV(logfile << "leastFrequencyKey: " << leastFrequencyKey << ", leastFrequency: " << leastFrequency << endl;);

                        // evcit LFU page of b1
                        typename key_to_value_type::iterator it_b1_tmp	= b1.find(leastFrequencyKey);
                        assert(it_b1_tmp != b1.end() );
                        b1.erase(it_b1_tmp);
                        b1_key.remove(leastFrequencyKey);
                        // evict LFU page of b1 from fList
                        fList.erase(leastFrequencyKey);

                        PRINTV(logfile << "Case IV evicts LFU page from b1: " << leastFrequencyKey << "++frequency: " << fList.find(leastFrequencyKey)->second << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                    }
                    else {
                        ///find the least frequency in fList
                        ///store the least frequency in fList
                        int leastFrequency = 9999999;
                        ///store the key of least frequency in fList
                        long long leastFrequencyKey = 0;

                        ///check starting from MRU position of t1, select the least frequency one, if tie, select the one near LRU position
                        typename key_tracker_type::iterator itMRU = t1_key.end();
                        itMRU--;
                        while(itMRU != t1_key.begin()) {
                            typename key_to_value_type_frequencyList::iterator itfList = fList.find(*itMRU);
                            if(itfList->second <= leastFrequency) {
                                leastFrequency = itfList->second;
                                leastFrequencyKey = itfList->first;
                            }
                            itMRU--;
                        }

                        itMRU = t1_key.begin();
                        typename key_to_value_type_frequencyList::iterator itfList = fList.find(*itMRU);
                        assert(itfList != fList.end() );
                        if(itfList->second <= leastFrequency) {
                            leastFrequency = itfList->second;
                            leastFrequencyKey = itfList->first;
                        }

                        PRINTV(logfile << "leastFrequencyKey: " << leastFrequencyKey << ", leastFrequency: " << leastFrequency << endl;);

                        typename key_to_value_type::iterator it_t1a_tmp	= t1a.find(leastFrequencyKey);
                        typename key_to_value_type::iterator it_t1b_tmp	= t1b.find(leastFrequencyKey);

                        if(it_t1a_tmp != t1a.end()) {
                            assert(it_t1b_tmp == t1b.end());
                            ///evcit LFU page of t1a
                            t1a.erase(it_t1a_tmp);
                            t1_key.remove(leastFrequencyKey);
                            ///evict LFU page of b1 from fList
                            fList.erase(leastFrequencyKey);

                            PRINTV(logfile << "Case IV evicts LFU page from t1a: " << leastFrequencyKey << "++frequency: " << fList.find(leastFrequencyKey)->second << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                        }

                        if(it_t1b_tmp != t1b.end()) {
                            assert(it_t1a_tmp == t1a.end());
                            ///evcit LFU page of t1b
                            t1b.erase(it_t1b_tmp);
                            t1_key.remove(leastFrequencyKey);
                            ///evict LFU page of b1 from fList
                            fList.erase(leastFrequencyKey);

                            PRINTV(logfile << "Case IV evicts LFU page from t1b: " << leastFrequencyKey << "++frequency: " << fList.find(leastFrequencyKey)->second<< "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                        }
                    }
                }
            }
            return (status | PAGEMISS);
        }
        ///*************************************************************************************************
        // Hibachi Case V: x is cache miss
        else {
            PRINTV(logfile << "Case V miss on key: " << k << endl;);

            if((t1a.size() + t1b.size() + b1.size()) == ((unsigned)NVM_capacity+DRAM_capacity)) {
                ///b1 is not empty
                if(t1a.size() + t1b.size() < ((unsigned)NVM_capacity+DRAM_capacity)) {
                    // find the least frequency in fList
                    // store the least frequency in fList
                    int leastFrequency = 9999999;
                    // store the key of least frequency in fList
                    long long leastFrequencyKey = 0;

                    // check starting from MRU position of b1, select the least frequency one, if tie, select the one near LRU position
                    typename key_tracker_type::iterator itMRU = b1_key.end();
                    itMRU--;
                    while(itMRU != b1_key.begin()) {
                        typename key_to_value_type_frequencyList::iterator itfList = fList.find(*itMRU);
                        assert(itfList != fList.end());
                        if(itfList->second <= leastFrequency) {
                            leastFrequency = itfList->second;
                            leastFrequencyKey = itfList->first;
                        }
                        itMRU--;
                    }

                    itMRU = b1_key.begin();
                    typename key_to_value_type_frequencyList::iterator itfList = fList.find(*itMRU);
                    assert(itfList != fList.end() );
                    if(itfList->second <= leastFrequency) {
                        leastFrequency = itfList->second;
                        leastFrequencyKey = itfList->first;
                    }

                    PRINTV(logfile << "leastFrequencyKey: " << leastFrequencyKey << ", leastFrequency: " << leastFrequency << endl;);

                    ///evcit LFU page of b1
                    typename key_to_value_type::iterator it_b1_tmp	= b1.find(leastFrequencyKey);
                    assert(it_b1_tmp != b1.end());
                    b1.erase(it_b1_tmp);
                    b1_key.remove(leastFrequencyKey);
                    ///evict LFU page of b1 from fList
                    fList.erase(leastFrequencyKey);

                    PRINTV(logfile << "Case V evicts LFU page from b1: " << leastFrequencyKey << "++frequency: " << fList.find(leastFrequencyKey)->second << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);

                    const V v = _fn(k, value);
                    ///E&B
                    REPLACE(k, v, p, status);
                }
                ///b1 is empty
                else {
                    ///find the least frequency in fList
                    ///store the least frequency in fList
                    int leastFrequency = 9999999;
                    ///store the key of least frequency in fList
                    long long leastFrequencyKey = 0;

                    ///check starting from MRU position of t1, select the least frequency one, if tie, select the one near LRU position
                    typename key_tracker_type::iterator itMRU = t1_key.end();
                    itMRU--;
                    while(itMRU != t1_key.begin()) {
                        typename key_to_value_type_frequencyList::iterator itfList = fList.find(*itMRU);
                        assert(itfList != fList.end() );
                        if(itfList->second <= leastFrequency) {
                            leastFrequency = itfList->second;
                            leastFrequencyKey = itfList->first;
                        }
                        itMRU--;
                    }

                    itMRU = t1_key.begin();
                    typename key_to_value_type_frequencyList::iterator itfList = fList.find(*itMRU);
                    assert(itfList != fList.end() );
                    if(itfList->second <= leastFrequency) {
                        leastFrequency = itfList->second;
                        leastFrequencyKey = itfList->first;
                    }

                    PRINTV(logfile << "leastFrequencyKey: " << leastFrequencyKey << ", leastFrequency: " << leastFrequency << endl;);

                    typename key_to_value_type::iterator it_t1a_tmp	= t1a.find(leastFrequencyKey);
                    typename key_to_value_type::iterator it_t1b_tmp	= t1b.find(leastFrequencyKey);

                    if(it_t1a_tmp != t1a.end()) {
                        assert(it_t1b_tmp == t1b.end());
                        ///evcit LFU page of t1a
                        t1a.erase(it_t1a_tmp);
                        t1_key.remove(leastFrequencyKey);
                        ///evict LFU page of b1 from fList
                        fList.erase(leastFrequencyKey);

                        PRINTV(logfile << "Case V evicts LFU page from t1a: " << leastFrequencyKey << "++frequency: " << fList.find(leastFrequencyKey)->second << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                    }

                    if(it_t1b_tmp != t1b.end()) {
                        assert(it_t1a_tmp == t1a.end());
                        ///evcit LFU page of t1b
                        t1b.erase(it_t1b_tmp);
                        t1_key.remove(leastFrequencyKey);
                        ///evict LFU page of b1 from fList
                        fList.erase(leastFrequencyKey);

                        PRINTV(logfile << "Case V evicts LFU page from t1b: " << leastFrequencyKey << "++frequency: " << fList.find(leastFrequencyKey)->second << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                    }
                }
            }
            else if((t1a.size() + t1b.size() + b1.size()) < ((unsigned)NVM_capacity+DRAM_capacity)) {
                if((t1a.size() + t1b.size() + t2.size() + b1.size() + b2.size()) >= ((unsigned)NVM_capacity+DRAM_capacity)) {
                    if((t1a.size() + t1b.size() + t2.size() + b1.size() + b2.size()) == 2 * ((unsigned)NVM_capacity+DRAM_capacity)) {
                        typename key_tracker_type::iterator itLRU = b2_key.begin();
                        assert(itLRU != b2_key.end());
                        typename key_to_value_type::iterator it = b2.find(*itLRU);
                        assert(it != b2.end() );
                        b2.erase(it);
                        b2_key.remove(*itLRU);
                    }
                    const V v = _fn(k, value);
                    ///E&B
                    REPLACE(k, v, p, status);
                }
            }
            const V v = _fn(k, value);

            // write request
            if(status & WRITE) {
                // Record k as most-recently-used key
                typename key_tracker_type::iterator itNew = t2_key.insert(t2_key.end(), k);
                // NVRAM is not full
                if(t1b.size() + t2.size() < (unsigned)NVM_capacity) {
                    PRINTV(logfile << "Case V, write miss and NVRAM not full: " << k << endl;);
                    // Create the key-value entry,
                    // linked to the usage record.
                    assert(t2.size()+t1b.size() < (unsigned)NVM_capacity);
                    t2.insert(make_pair(k, make_pair(v, itNew)));

                    seqListInsert(k);

                    // create an item in fList
                    fList.insert(make_pair(k, 0));

                    PRINTV(logfile << "Case V insert dirty key to t2: " << k << "++frequency: " << fList.find(k)->second << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);

                }
                // NVRAM is full
                else {
                    // NVRAM is filled with dirty pages
                    if(t2.size() == (unsigned)NVM_capacity) {
                        PRINTV(logfile << "Case V, write miss and NVRAM is full, t2.size() == NVRAM: " << k << endl;);
                        ///select the LRU page of t2
                        typename key_tracker_type::iterator itLRU = t2_key.begin();
                        assert(itLRU != t2_key.end());
                        typename key_to_value_type::iterator it = t2.find(*itLRU);
                        assert(it != t2.end());
                        ///insert LRU page of t2 to LRU of t1a
                        typename key_tracker_type::iterator itNewTmp = t1_key.insert(t1_key.begin(), *itLRU);
                        // Create the key-value entry,
                        // linked to the usage record.
                        const V v_tmp = _fn(*itLRU, it->second.first);
                        assert(t1a.size() < DRAM_capacity);
                        t1a.insert(make_pair(*itLRU, make_pair(v_tmp, itNewTmp)));
                        ///flush back and evcit the LRU page of t2
                        t2.erase(it);
                        t2_key.remove(*itLRU);

                        // DiskSim format Request_arrival_time Device_number Block_number Request_size Request_flags
                        // Device_number is set to 1. About Request_flags, 0 is for write and 1 is for read
                        PRINTV(logfile << "flushing to disksim input file" <<  endl;);
                        PRINTV(DISKSIMINPUTSTREAM<<setfill(' ')<<left<<fixed<<setw(25)<<v.getReq().issueTime<<left<<setw(8)<<"0"<<left<<fixed<<setw(12)<<*itLRU<<left<<fixed<<setw(8)<<"1"<<"0"<<endl;);

                        seqListEvict(*itLRU);

                        PRINTV(logfile << "Case V (NVM is filled with dirty pages) evicts and flushes a dirty page " << *itLRU << "++frequency: " << fList.find(*itLRU)->second << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                        totalPageWriteToStorage++;

                        ///insert the missed page to MRU of t2
                        // Record k as most-recently-used key
                        typename key_tracker_type::iterator itNew = t2_key.insert(t2_key.end(), k);
                        // Create the key-value entry,
                        // linked to the usage record.
                        assert(t2.size()+t1b.size() < (unsigned)NVM_capacity);
                        t2.insert(make_pair(k, make_pair(v, itNew)));

                        seqListInsert(k);

                        ///create an item in fList
                        fList.insert(make_pair(k, 0));

                        PRINTV(logfile << "Case V insert a dirty page to t2: " << k << "++frequency: " << fList.find(k)->second<< "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                    }
                    ///NVRAM is full but is not filled with dirty pages
                    else {
                        PRINTV(logfile << "Case V, write miss and NVRAM is full, t2.size() < NVRAM: " << k << endl;);
                        ///migrate a clean page from t1b to t1a
                        typename key_to_value_type::iterator it_t1b_tmp = t1b.begin();
                        assert(it_t1b_tmp != t1b.end());
                        ///make sure the random page from t1b is a real page
                        assert( !(it_t1b_tmp->second.first.getReq().flags & PCR) );
                        typename key_tracker_type::iterator it_t1b_key_tmp = t1_key.end();
                        const V v_tmp = _fn(it_t1b_tmp->first, it_t1b_tmp->second.first);
                        assert(t1a.size() < DRAM_capacity);
                        t1a.insert(make_pair(it_t1b_tmp->first, make_pair(v_tmp, it_t1b_key_tmp)));

                        t1b.erase(it_t1b_tmp);
                        PRINTV(logfile << "Case V migrate a clean page from t1b to t1a: " << it_t1b_tmp->first << "++frequency: " << fList.find(it_t1b_tmp->first)->second << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);

                        ///insert the missed page to MRU of t2
                        // Record k as most-recently-used key
                        typename key_tracker_type::iterator itNew = t2_key.insert(t2_key.end(), k);
                        // Create the key-value entry,
                        // linked to the usage record.
                        assert(t2.size()+t1b.size() < (unsigned)NVM_capacity);
                        t2.insert(make_pair(k, make_pair(v, itNew)));

                        seqListInsert(k);

                        ///create an item in fList
                        fList.insert(make_pair(k, 0));

                        PRINTV(logfile << "Case V insert a dirty page to t2: " << k << "++frequency: " << fList.find(k)->second << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                    }
                }
            }
            // read request
            else {
                // Record k as most-recently-used key
                typename key_tracker_type::iterator itNew = t1_key.insert(t1_key.end(), k);
                // Create the key-value entry,
                // linked to the usage record.
                if(t1a.size() < DRAM_capacity) {
                    PRINTV(logfile << "Case V, read miss and DRAM is not full: " << k << endl;);
                    assert(t1a.size() < DRAM_capacity);
                    t1a.insert(make_pair(k, make_pair(v, itNew)));

                    // create an item in fList
                    fList.insert(make_pair(k, 1));

                    PRINTV(logfile << "Case V insert a clean page to t1a: " << k << "++frequency: " << fList.find(k)->second << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                }
                else {
                    PRINTV(logfile << "Case V, read miss and DRAM is full and NVRAM is not full: " << k << endl;);
                    assert(t2.size()+t1b.size() < (unsigned)NVM_capacity);
                    t1b.insert(make_pair(k, make_pair(v, itNew)));

                    // create an item in fList
                    fList.insert(make_pair(k, 1));

                    PRINTV(logfile << "Case V insert a clean page to t1b: " << k << "++frequency: " << fList.find(k)->second << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                }
            }
            return (status | PAGEMISS);
        }
        // should never reach here
        assert(0);
        return 0;
    } // end operator access

    ///*************************************************************************************************
    // Hibachi subroutine
    void REPLACE(const K &k, const V &v, int p, uint32_t status) {
        // REPLACE can only be triggered only if both NVRAM and DRAM are full
        size_t DRAM_capacity = (size_t)_capacity;
        int NVM_capacity = nvramSize;
        assert(t1a.size() + t1b.size() + t2.size() == ((unsigned)NVM_capacity+DRAM_capacity));

        assert(t1a.size() == DRAM_capacity);

        PRINTV(logfile << "REPLACE begins" << endl;);
        typename key_to_value_type::iterator it = b2.find(k);
        // evict from clean cache side
        if((t1a.size()+t1b.size() > 0) && ( (t1a.size() + t1b.size() > unsigned(p) ) || ((it != b2.end()) && (t1a.size() + t1b.size() == unsigned(p))))) {
            PRINTV(logfile << "REPLACE from clean page cache" << endl;);
            typename key_tracker_type::iterator itLRU = t1_key.begin();
            assert(itLRU != t1_key.end());

            // find the least frequency in fList
            // store the least frequency in fList
            int leastFrequency = 9999999;
            // store the key of least frequency in fList
            long long leastFrequencyKey = 0;

            // check starting from MRU position of t1, select the least frequency one, if tie, select the one near LRU position
            typename key_tracker_type::iterator itMRU = t1_key.end();
            itMRU--;
            while(itMRU != t1_key.begin()) {
                typename key_to_value_type_frequencyList::iterator itfList = fList.find(*itMRU);
                assert(itfList != fList.end() );
                if(itfList->second <= leastFrequency) {
                    leastFrequency = itfList->second;
                    leastFrequencyKey = itfList->first;
                }
                itMRU--;
            }

            itMRU = t1_key.begin();
            typename key_to_value_type_frequencyList::iterator itfList = fList.find(*itMRU);
            assert(itfList != fList.end() );
            if(itfList->second <= leastFrequency) {
                leastFrequency = itfList->second;
                leastFrequencyKey = itfList->first;
            }

            PRINTV(logfile << "leastFrequencyKey: " << leastFrequencyKey << ", leastFrequency: " << leastFrequency << endl;);

            typename key_to_value_type::iterator it_t1a_tmp	= t1a.find(leastFrequencyKey);
            typename key_to_value_type::iterator it_t1b_tmp	= t1b.find(leastFrequencyKey);

            if(it_t1a_tmp != t1a.end()) {
                assert(it_t1b_tmp == t1b.end());

                // insert LFU page of t1 to MRU of b1
                typename key_tracker_type::iterator itNew = b1_key.insert(b1_key.end(), leastFrequencyKey);
                const V v_tmp = _fn(leastFrequencyKey, it_t1a_tmp->second.first);
                b1.insert(make_pair(leastFrequencyKey, make_pair(v_tmp, itNew)));
                PRINTV(logfile << "Case REPLACE insert LFU page of t1a to b1: " << leastFrequencyKey << "++frequency: " << fList.find(leastFrequencyKey)->second << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);

                // evcit LFU page of t1a
                t1a.erase(it_t1a_tmp);
                t1_key.remove(leastFrequencyKey);

                PRINTV(logfile << "Case REPLACE evicts LFU page from t1a: " << leastFrequencyKey << "++frequency: " << fList.find(leastFrequencyKey)->second << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
            }

            if(it_t1b_tmp != t1b.end()) {
                assert(it_t1a_tmp == t1a.end());

                // insert LFU page of t1 to MRU of b1
                typename key_tracker_type::iterator itNew = b1_key.insert(b1_key.end(), leastFrequencyKey);
                const V v_tmp = _fn(leastFrequencyKey, it_t1b_tmp->second.first);
                b1.insert(make_pair(leastFrequencyKey, make_pair(v_tmp, itNew)));
                PRINTV(logfile << "Case REPLACE insert LFU page of t1b to b1: " << leastFrequencyKey << "++frequency: " << fList.find(leastFrequencyKey)->second << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);

                // evcit LFU page of t1b
                t1b.erase(it_t1b_tmp);
                t1_key.remove(leastFrequencyKey);

                PRINTV(logfile << "Case REPLACE evicts LFU page from t1b: " << leastFrequencyKey << "++frequency: " << fList.find(leastFrequencyKey)->second << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
            }

            if( (status & WRITE) && (t2.size() == (unsigned)NVM_capacity) ) {
                assert(t1b.size() == 0);

                // select the LRU page of t2
                typename key_tracker_type::iterator itLRU = t2_key.begin();
                assert(itLRU != t2_key.end());
                typename key_to_value_type::iterator it = t2.find(*itLRU);
                assert(it != t2.end());
                // insert LRU page of t2 to LRU of t1a
                typename key_tracker_type::iterator itNewTmp = t1_key.insert(t1_key.begin(), *itLRU);
                // Create the key-value entry,
                // linked to the usage record.
                const V v_tmp = _fn(*itLRU, it->second.first);
                assert(t1a.size() < DRAM_capacity);
                t1a.insert(make_pair(*itLRU, make_pair(v_tmp, itNewTmp)));
                // flush back and evcit the LRU page of t2
                t2.erase(it);
                t2_key.remove(*itLRU);

                // DiskSim format Request_arrival_time Device_number Block_number Request_size Request_flags
                // Device_number is set to 1. About Request_flags, 0 is for write and 1 is for read
                PRINTV(logfile << "flushing to disksim input file" <<  endl;);
                PRINTV(DISKSIMINPUTSTREAM<<setfill(' ')<<left<<fixed<<setw(25)<<v.getReq().issueTime<<left<<setw(8)<<"0"<<left<<fixed<<setw(12)<<*itLRU<<left<<fixed<<setw(8)<<"1"<<"0"<<endl;);

                seqListEvict(*itLRU);

                PRINTV(logfile << "Case REPLACE (NVM is filled with dirty pages) evicts and flushes a dirty page " << *itLRU << "++frequency: " << fList.find(*itLRU)->second << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                totalPageWriteToStorage++;
            }
        }
        // evcit from dirty cache side
        else {
            PRINTV(logfile << "REPLACE from dirty page cache" << endl;);

            // find the max length in seqList
            // store the maximum length in seqList
            int maxLength = 0;
            // store the key of maximum length in seqList
            int maxLengthKey = 0;
            typename key_to_value_type_seqList::iterator itSeqList = seqList.begin();
            while(itSeqList != seqList.end()) {
                if(itSeqList->second > maxLength) {
                    maxLength = itSeqList->second;
                    maxLengthKey = itSeqList->first;
                }
                itSeqList++;
            }
            PRINTV(logfile << "maxLengthKey " << maxLengthKey << " ,maxLength " << maxLength <<  endl;);

            // +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

            // maxLength in the seqList above the threshold, flush the sequence
            if(maxLength > threshold) {
                PRINTV(logfile << "maxLength is above threshold: " << threshold << endl;);
                // flush to HDD
                // DiskSim format Request_arrival_time Device_number Block_number Request_size Request_flags
                // Device_number is set to 1. About Request_flags, 0 is for write and 1 is for read
                PRINTV(logfile << "flushing to disksim input file" <<  endl;);
                PRINTV(DISKSIMINPUTSTREAM<<setfill(' ')<<left<<fixed<<setw(25)<<v.getReq().issueTime<<left<<setw(8)<<"0"<<left<<fixed<<setw(12)<<maxLengthKey<<left<<fixed<<setw(8)<<maxLength<<"0"<<endl;);

                //keep score of total page write number to storage
                totalPageWriteToStorage = totalPageWriteToStorage + maxLength;

                // insert maxLengthKey to b2
                typename key_to_value_type::iterator itFirst = t2.find(maxLengthKey);
                assert(itFirst != t2.end());
                typename key_tracker_type::iterator itNew = b2_key.insert(b2_key.end(), maxLengthKey);
                const V v_tmp = _fn(maxLengthKey, itFirst->second.first);
                b2.insert(make_pair(maxLengthKey, make_pair(v_tmp, itNew)));
                PRINTV(logfile << "REPLACE insert key to b2: " << maxLengthKey << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);

                // insert from maxLengthKey+1 to maxLengthKey+maxLength to t1
                int i=1;
                while(i<maxLength) {
                    typename key_to_value_type::iterator it = t2.find(maxLengthKey+i);
                    assert(it != t2.end());
                    ///insert to LRU position instead of MRU position
                    typename key_tracker_type::iterator itNew = t1_key.insert(t1_key.begin(), maxLengthKey+i);
                    V v_tmp = _fn(maxLengthKey+i, it->second.first);
                    t1b.insert(make_pair(maxLengthKey+i, make_pair(v_tmp, itNew)));
                    PRINTV(logfile << "REPLACE insert to LRU of t1: " << maxLengthKey+i << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                    i++;
                }

                // evict from maxLengthKey to maxLengthKey+maxLength from t2
                i=0;
                while(i<maxLength) {
                    typename key_to_value_type::iterator it = t2.find(maxLengthKey+i);
                    assert(it != t2.end());
                    t2.erase(it);
                    t2_key.remove(maxLengthKey+i);
                    PRINTV(logfile << "REPLACE erase seq page: " << maxLengthKey+i << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                    i++;
                }
                // erase maxLengthKey from seqList
                seqList.erase(maxLengthKey);
                PRINTV(logfile << "REPLACE erase maxLengthKey key from seqList: " << maxLengthKey << endl;);
            }

            // ------------------------------------------------------------------------

            // maxLength is smaller than threshold, follow LRU order
            else {
                // find the least frequency in fList
                // store the least frequency in fList
                int leastFrequency = 9999999;
                // store the key of least frequency in
                long long leastFrequencyKey = 0;

                // check starting from MRU position of t1, select the least frequency one, if tie, select the one near LRU position
                typename key_tracker_type::iterator itMRU = t1_key.end();
                itMRU--;
                while(itMRU != t1_key.begin()) {
                    typename key_to_value_type_frequencyList::iterator itfList = fList.find(*itMRU);
                    assert(itfList != fList.end() );
                    if(itfList->second <= leastFrequency) {
                        leastFrequency = itfList->second;
                        leastFrequencyKey = itfList->first;
                    }
                    itMRU--;
                }

                itMRU = t1_key.begin();
                typename key_to_value_type_frequencyList::iterator itfList = fList.find(*itMRU);
                assert(itfList != fList.end() );
                if(itfList->second <= leastFrequency) {
                    leastFrequency = itfList->second;
                    leastFrequencyKey = itfList->first;
                }

                PRINTV(logfile << "leastFrequencyKey: " << leastFrequencyKey << ", leastFrequency: " << leastFrequency << endl;);

                // find the LRU page of t2 and its frequency
                typename key_tracker_type::iterator itLRU_t2 = t2_key.begin();
                typename key_to_value_type::iterator it_t2_tmp	= t2.find(*itLRU_t2);
                assert(it_t2_tmp != t2.end() );
                itfList = fList.find(*itLRU_t2);
                assert(itfList != fList.end() );
                int fLRU_t2 = itfList->second;

                // frequency of LRU page of t2 is bigger than LFU of t1
                if(fLRU_t2 > leastFrequency) {
                    typename key_to_value_type::iterator it_t1a_tmp	= t1a.find(leastFrequencyKey);
                    typename key_to_value_type::iterator it_t1b_tmp	= t1b.find(leastFrequencyKey);

                    if(it_t1a_tmp != t1a.end()) {
                        assert(it_t1b_tmp == t1b.end());

                        // insert LFU page of t1 to MRU of b1
                        typename key_tracker_type::iterator itNew = b1_key.insert(b1_key.end(), leastFrequencyKey);
                        const V v_tmp = _fn(leastFrequencyKey, it_t1a_tmp->second.first);
                        b1.insert(make_pair(leastFrequencyKey, make_pair(v_tmp, itNew)));
                        PRINTV(logfile << "Case REPLACE insert LFU page of t1a to b1: " << leastFrequencyKey << "++frequency: " << fList.find(leastFrequencyKey)->second << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);

                        // evcit LFU page of t1a
                        t1a.erase(it_t1a_tmp);
                        t1_key.remove(leastFrequencyKey);

                        PRINTV(logfile << "Case REPLACE evicts LFU page from t1a: " << leastFrequencyKey << "++frequency: " << fList.find(leastFrequencyKey)->second << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);

                        // insert LRU page of t2 to t1a
                        itNew = t1_key.insert(t1_key.end(), *itLRU_t2);
                        const V v_tmp_2 = _fn(*itLRU_t2, it_t2_tmp->second.first);
                        assert(t1a.size() < DRAM_capacity);
                        t1a.insert(make_pair(*itLRU_t2, make_pair(v_tmp_2, itNew)));
                        // evcit and flush LRU page of t2
                        t2.erase(it_t2_tmp);
                        t2_key.remove(*itLRU_t2);

                        // DiskSim format Request_arrival_time Device_number Block_number Request_size Request_flags
                        // Device_number is set to 1. About Request_flags, 0 is for write and 1 is for read
                        PRINTV(logfile << "flushing to disksim input file" <<  endl;);
                        PRINTV(DISKSIMINPUTSTREAM<<setfill(' ')<<left<<fixed<<setw(25)<<v.getReq().issueTime<<left<<setw(8)<<"0"<<left<<fixed<<setw(12)<<*itLRU_t2<<left<<fixed<<setw(8)<<"1"<<"0"<<endl;);

                        seqListEvict(*itLRU_t2);

                        PRINTV(logfile << "REPLACE evicts and flushes a dirty page " << *itLRU_t2 << "++frequency: " << fList.find(leastFrequencyKey)->second << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                        totalPageWriteToStorage++;
                    }

                    if(it_t1b_tmp != t1b.end()) {
                        assert(it_t1a_tmp == t1a.end());

                        // insert LFU page of t1 to MRU of b1
                        typename key_tracker_type::iterator itNew = b1_key.insert(b1_key.end(), leastFrequencyKey);
                        const V v_tmp = _fn(leastFrequencyKey, it_t1b_tmp->second.first);
                        b1.insert(make_pair(leastFrequencyKey, make_pair(v_tmp, itNew)));
                        PRINTV(logfile << "Case REPLACE insert LFU page of t1b to b1: " << leastFrequencyKey << "++frequency: " << fList.find(leastFrequencyKey)->second << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);

                        // evcit LFU page of t1b
                        t1b.erase(it_t1b_tmp);
                        t1_key.remove(leastFrequencyKey);

                        PRINTV(logfile << "Case REPLACE evicts LFU page from t1b: " << leastFrequencyKey << "++frequency: " << fList.find(leastFrequencyKey)->second << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);

                        // insert LRU page of t2 to t1b
                        itNew = t1_key.insert(t1_key.end(), *itLRU_t2);
                        const V v_tmp_2 = _fn(*itLRU_t2, it_t2_tmp->second.first);
                        assert(t1b.size() + t2.size() < (unsigned)NVM_capacity);
                        t1b.insert(make_pair(*itLRU_t2, make_pair(v_tmp_2, itNew)));
                        // evcit and flush LRU page of t2
                        t2.erase(it_t2_tmp);
                        t2_key.remove(*itLRU_t2);

                        // DiskSim format Request_arrival_time Device_number Block_number Request_size Request_flags
                        // Device_number is set to 1. About Request_flags, 0 is for write and 1 is for read
                        PRINTV(logfile << "flushing to disksim input file" <<  endl;);
                        PRINTV(DISKSIMINPUTSTREAM<<setfill(' ')<<left<<fixed<<setw(25)<<v.getReq().issueTime<<left<<setw(8)<<"0"<<left<<fixed<<setw(12)<<*itLRU_t2<<left<<fixed<<setw(8)<<"1"<<"0"<<endl;);

                        seqListEvict(*itLRU_t2);

                        PRINTV(logfile << "REPLACE evicts and flushes a dirty page " << *itLRU_t2 << "++frequency: " << fList.find(*itLRU_t2)->second << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                        totalPageWriteToStorage++;
                    }
                }
                // frequency of LRU page of t2 is smaller or equal to LFU of t1
                else {
                    // insert LRU page of t2 to MRU of b2
                    typename key_tracker_type::iterator itNew = b2_key.insert(b2_key.end(), *itLRU_t2);
                    const V v_tmp = _fn(*itLRU_t2, it_t2_tmp->second.first);
                    b2.insert(make_pair(*itLRU_t2, make_pair(v_tmp, itNew)));
                    PRINTV(logfile << "Case REPLACE insert LRU page of t2 to b2: " << leastFrequencyKey << "++frequency: " << fList.find(leastFrequencyKey)->second << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);

                    // evcit and flush LRU page of t2
                    t2.erase(it_t2_tmp);
                    t2_key.remove(*itLRU_t2);

                    // DiskSim format Request_arrival_time Device_number Block_number Request_size Request_flags
                    // Device_number is set to 1. About Request_flags, 0 is for write and 1 is for read
                    PRINTV(logfile << "flushing to disksim input file" <<  endl;);
                    PRINTV(DISKSIMINPUTSTREAM<<setfill(' ')<<left<<fixed<<setw(25)<<v.getReq().issueTime<<left<<setw(8)<<"0"<<left<<fixed<<setw(12)<<*itLRU_t2<<left<<fixed<<setw(8)<<"1"<<"0"<<endl;);

                    seqListEvict(*itLRU_t2);

                    PRINTV(logfile << "REPLACE evicts and flushes a dirty page " << *itLRU_t2 << "++frequency: " << fList.find(*itLRU_t2)->second << "** t1a size: "<< t1a.size()<< ", t1b size: "<< t1b.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
                    totalPageWriteToStorage++;
                }
            }
        }
    }

    // Hibachi subroutine
    void seqListInsert (const K &k) {
        PRINTV(logfile << "seqList inserts key " << k <<  endl;);
        int i = k;
        int j = k;

        ///left is smaller end and right is larger end
        int leftMost = k;
        int rightMost = k;

        int seqLength = 1;

        typename key_to_value_type::iterator itLeft = t2.find(--i);
        typename key_to_value_type::iterator itRight = t2.find(++j);

        while(itLeft != t2.end()) {
            PRINTV(logfile << "seqListUpdate find left key " << i <<  endl;);
            leftMost = i;
            seqLength++;
            itLeft = t2.find(--i);
        }
        while(itRight != t2.end()) {
            PRINTV(logfile << "seqListUpdate find right key " << j <<  endl;);
            rightMost = j;
            seqLength++;
            itRight = t2.find(++j);
        }
        PRINTV(logfile << "seqLength " << seqLength <<  endl;);
        if( (unsigned(leftMost) == k) && (unsigned(rightMost) != k) ) {
            PRINTV(logfile << "seqListUpdate leftMost is equal to k " << leftMost<< "," << rightMost <<  endl;);
            typename key_to_value_type_seqList::iterator itTmp = seqList.find(k+1);
            if (itTmp != seqList.end()) {
                seqList.erase(k+1);
                PRINTV(logfile << "seqListUpdate seqList has key - k+1 already, need to be erased first, k+1 is " << k+1 <<  endl;);
            }
            seqList.insert(make_pair(k, seqLength));
            PRINTV(logfile << "seqListUpdate seqList insert (key, value): (" << k << "," << seqLength << ")" << endl;);
        }
        else if( (unsigned(rightMost) == k) && (unsigned(leftMost) != k) ) {
            PRINTV(logfile << "seqListUpdate rightMost is equal to k " << leftMost<< "," << rightMost <<  endl;);
            seqList.erase(leftMost);
            seqList.insert(make_pair(leftMost, seqLength));
            PRINTV(logfile << "seqListUpdate seqList insert (key, value): (" << leftMost << "," << seqLength << ")" << endl;);
        }
        else if( (unsigned(rightMost) == k) && (unsigned(leftMost) == k) ) {
            PRINTV(logfile << "seqListUpdate leftMost and rightMost are equal to k " << leftMost<< "," << rightMost <<  endl;);
            seqList.insert(make_pair(leftMost, seqLength));
            PRINTV(logfile << "seqListUpdate seqList insert (key, value): (" << leftMost << "," << seqLength << ")" << endl;);
        }
        else if( (unsigned(rightMost) != k) && (unsigned(leftMost) != k) ) {
            PRINTV(logfile << "seqListUpdate leftMost and rightMost are not equal to k " << leftMost<< "," << rightMost <<  endl;);
            typename key_to_value_type_seqList::iterator itTmp = seqList.find(k+1);
            if (itTmp != seqList.end()) {
                seqList.erase(k+1);
                PRINTV(logfile << "seqListUpdate seqList has key - k+1 already, need to be erased first, k+1 is " << k+1 <<  endl;);
            }
            seqList.erase(leftMost);
            seqList.insert(make_pair(leftMost, seqLength));
            PRINTV(logfile << "seqListUpdate seqList insert (key, value): (" << leftMost << "," << seqLength << ")" << endl;);
        }
    }

    // Hibachi subroutine
    void seqListEvict(const K &k) {
        PRINTV(logfile << "seqList evicts key " << k <<  endl;);

        // find out which key value pair that "k" locates in
        int key;
        int value;

        typename key_to_value_type_seqList::iterator itSeqList = seqList.begin();
        while(itSeqList != seqList.end()) {
            if( ( unsigned(itSeqList->first) <= k) && ( unsigned(itSeqList->first + itSeqList->second - 1) >= k) ) {
                key = itSeqList->first;
                value = itSeqList->second;
                break;
            }
            itSeqList++;
        }
        PRINTV(logfile << "value " << value <<  endl;);

        if(value == 1) {
            // evict the key from seqList since it is the only item in the sequence
            seqList.erase(key);
        }
        else if(unsigned(key) == k) {
            seqList.erase(key);
            seqList.insert(make_pair(key+1, value-1));
            PRINTV(logfile << "seqListUpdate seqList insert (key, value): (" << key+1 << "," << value-1 << ")" << endl;);
        }
        else if( unsigned(key+value-1) == k ) {
            seqList.erase(key);
            seqList.insert(make_pair(key, value-1));
            PRINTV(logfile << "seqListUpdate seqList insert (key, value): (" << key << "," << value-1 << ")" << endl;);
        }
        else {
            seqList.erase(key);
            seqList.insert(make_pair(key, k-key));
            seqList.insert(make_pair(k+1, value-(k-key)-1));
            PRINTV(logfile << "seqListUpdate seqList insert (key, value): (" << key << "," << k-key << ") and (key, value): (" << k+1 << "," << value-(k-key)-1 << ")" << endl;);
        }
    }

private:

    // The function to be cached
    V(*_fn)(const K & , V);
    // Maximum number of key-value pairs to be retained
    const size_t _capacity;

    unsigned levelMinusMinus;

    // Key access history clean page LRU list
    key_tracker_type t1_key;
    // Key-to-value lookup clean page in DRAM
    key_to_value_type t1a;
    // Key-to-value lookup clean page in NVRAM
    key_to_value_type t1b;
    // Key access history dirty page LRU list
    key_tracker_type t2_key;
    // Key-to-value lookup dirty page in NVRAM
    key_to_value_type t2;
    // Key access history clean ghost LRU list
    key_tracker_type b1_key;
    // Key-to-value lookup clean ghost pages
    key_to_value_type b1;
    // Key access history dirty ghost LRU list
    key_tracker_type b2_key;
    // Key-to-value lookup dirty ghost pages
    key_to_value_type b2;

    // Key-to-value lookup
    key_to_value_type_frequencyList fList;

    // Key-to-value lookup
    key_to_value_type_seqList seqList;
};

#endif //end hybrid
