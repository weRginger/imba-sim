//
// C++ Interface: darc
//
// Description: I/OCache when evict from dirty cache, evict the longest queue in order to take advantage of HDD
//Dirty/clean cache partition algorithm. Hit on clean ghost enlarge desired clean cache; hit on dirty ghost enlarge desired dirty cache.
//
// Author: Ziqi Fan, (C) 2014
//
// Copyright: See COPYING file that comes with this distribution
//

#ifndef IOCACHE_THRESHOLD_H
#define IOCACHE_THRESHOLD_H

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

extern int dirtyPageInCache;

///when evict from dirty cache side, if the maxLength in seqList is smaller than threshold, evcit according to LRU order
extern int threshold;

// Class providing fixed-size (by number of records)
// LRU-replacement cache of a function with signature
// V f(K)
template <typename K, typename V>
class IOCACHE_THRESHOLD : public TestCache<K, V>
{
public:
// Key access history, most recent at back
    typedef list<K> key_tracker_type;
// Key to value and key history iterator
    typedef map
    < K, pair<V, typename key_tracker_type::iterator> > 	key_to_value_type;

    typedef map
    < int, int > 	key_to_value_type_seqList;
// Constuctor specifies the cached function and
// the maximum number of records to be stored.
    IOCACHE_THRESHOLD(
        V(*f)(const K & , V),
        size_t c,
        unsigned levelMinus
    ) : _fn(f) , _capacity(c), levelMinusMinus(levelMinus)  {
        ///ARH: Commented for single level cache implementation
//         assert ( _capacity!=0 );
    }
    // Obtain value of the cached function for k

    uint32_t access(const K &k  , V &value, uint32_t status) {
        PRINTV(logfile << endl;);
        ///ziqi: p denotes the length of t1 and (_capacity - p) denotes the lenght of t2
        static int p=0;

        assert((t1.size() + t2.size()) <= _capacity);
        assert((t1.size() + t2.size() + b1.size() + b2.size()) <= 2*_capacity);
        assert((t1.size() + b1.size()) <= _capacity);
        assert((t2.size() + b2.size()) <= 2*_capacity);
        assert(_capacity != 0);
        PRINTV(logfile << "Access key: " << k << endl;);

        ///Keep track of the number of dirty page in cache
        ///in order to know how many pages did not have the chance to be flushed
        dirtyPageInCache = int(t2.size());
        PRINTV(logfile << "Dirty Page Number In Cache: " << dirtyPageInCache << endl;);

// Attempt to find existing record
        const typename key_to_value_type::iterator it_t1	= t1.find(k);
        const typename key_to_value_type::iterator it_t2	= t2.find(k);
        const typename key_to_value_type::iterator it_b1	= b1.find(k);
        const typename key_to_value_type::iterator it_b2	= b2.find(k);
        //const typename key_to_value_type::iterator itNew	= _key_to_value.find(k);

        ///ziqi: IOCache Case I: x hit in t1, then move x to t1 if it's a read, or to t2 if it's a write
        if((it_t1 != t1.end()) && (it_t2 == t2.end())) {
            assert(!((it_t1 != t1.end()) && (it_t2 != t2.end())));
            ///ziqi: if it is a write request
            if(status & WRITE) {
                PRINTV(logfile << "Case I Write hit on t1 " << k << endl;);
                t1.erase(it_t1);
                t1_key.remove(k);
                PRINTV(logfile << "Case I erase a clean page from t1: " << k << "** t1 size: "<< t1.size()<< ", t2 size: "<< t2.size() << "** b1 size: "<< b1.size()<< ", b2 size: "<< b2.size() <<endl;);
                const V v = _fn(k, value);
                // Record k as most-recently-used key
                typename key_tracker_type::iterator itNew
                    = t2_key.insert(t2_key.end(), k);
                // Create the key-value entry,
                // linked to the usage record.
                t2.insert(make_pair(k, make_pair(v, itNew)));
                PRINTV(logfile << "Case I insert a dirty page to t2: " << k << "** t1 size: "<< t1.size()<< ", t2 size: "<< t2.size() << "** b1 size: "<< b1.size()<< ", b2 size: "<< b2.size() <<endl;);
                ///update seqList
                seqListUpdate(k);
            }
            ///ziqi: if it is a read request
            else {
                PRINTV(logfile << "Case I Read hit on t1 " << k << endl;);
                t1.erase(it_t1);
                t1_key.remove(k);
                PRINTV(logfile << "Case I erase a clean page from t1: " << k << "** t1 size: "<< t1.size()<< ", t2 size: "<< t2.size() << "** b1 size: "<< b1.size()<< ", b2 size: "<< b2.size() <<endl;);
                const V v = _fn(k, value);
                // Record k as most-recently-used key
                typename key_tracker_type::iterator itNew
                    = t1_key.insert(t1_key.end(), k);
                // Create the key-value entry,
                // linked to the usage record.
                t1.insert(make_pair(k, make_pair(v, itNew)));
                PRINTV(logfile << "Case I insert a clean page to t1: " << k << "** t1 size: "<< t1.size()<< ", t2 size: "<< t2.size() << "** b1 size: "<< b1.size()<< ", b2 size: "<< b2.size() <<endl;);
            }
            return (status | PAGEHIT | BLKHIT);

        }

        ///ziqi: IOCache Case II: x hit in t2, then move x to MRU of t2
        if((it_t2 != t2.end()) && (it_t1 == t1.end())) {
            PRINTV(logfile << "Case II Read or Write hit on t2 " << k << endl;);
            assert(!((it_t1 != t1.end()) && (it_t2 != t2.end())));
            t2.erase(it_t2);
            t2_key.remove(k);
            PRINTV(logfile << "Case II erase a dirty page from t2: " << k << "** t1 size: "<< t1.size()<< ", t2 size: "<< t2.size() << "** b1 size: "<< b1.size()<< ", b2 size: "<< b2.size() <<endl;);
            const V v = _fn(k, value);
            // Record k as most-recently-used key
            typename key_tracker_type::iterator itNew = t2_key.insert(t2_key.end(), k);
            // Create the key-value entry,
            // linked to the usage record.
            t2.insert(make_pair(k, make_pair(v, itNew)));
            PRINTV(logfile << "Case II insert a dirty page to t2: " << k << "** t1 size: "<< t1.size()<< ", t2 size: "<< t2.size() << "** b1 size: "<< b1.size()<< ", b2 size: "<< b2.size() <<endl;);
            return (status | PAGEHIT | BLKHIT);

        }
        ///ziqi: IOCache Case III: x hit in b1, then enlarge t1, and move x from b1 to t1 if it's a read or to t2 if it's a write
        else if(it_b1 != b1.end()) {
            ///ziqi: delta denotes the step in each ADAPTATION
            int delta;

            PRINTV(logfile << "Case III Hit on b1: " << k << endl;);

            ///Disk read after a cache miss
            ///ziqi: DiskSim format Request_arrival_time Device_number Block_number Request_size Request_flags
            ///ziqi: Device_number is set to 1. About Request_flags, 0 is for write and 1 is for read
            PRINTV(DISKSIMINPUTSTREAM << setfill(' ')<<left<<fixed<<setw(25)<<value.getReq().issueTime<<left<<setw(8)<<"0"<<left<<fixed<<setw(12)<<k<<left<<fixed<<setw(8)<<"1"<<"1"<<endl;);

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

            REPLACE(k, v, p);

            b1.erase(it_b1);
            b1_key.remove(k);

            PRINTV(logfile << "Case III evicts b1 " << k << "** t1 size: "<< t1.size()<< ", t2 size: "<< t2.size() << "** b1 size: "<< b1.size()<< ", b2 size: "<< b2.size() <<endl;);

            ///ziqi: if it is a write request
            if(status & WRITE) {
                // Record k as most-recently-used key
                typename key_tracker_type::iterator itNew = t2_key.insert(t2_key.end(), k);
                // Create the key-value entry,
                // linked to the usage record.
                t2.insert(make_pair(k, make_pair(v, itNew)));
                PRINTV(logfile << "Case III insert a dirty page to t2: " << k << "** t1 size: "<< t1.size()<< ", t2 size: "<< t2.size() << "** b1 size: "<< b1.size()<< ", b2 size: "<< b2.size() <<endl;);
                ///update seqList
                seqListUpdate(k);

                while(t1.size() + b1.size() > _capacity) {
                    if(b1.size() > 0) {
                        typename key_tracker_type::iterator itLRU = b1_key.begin();
                        assert(itLRU != b1_key.end());
                        typename key_to_value_type::iterator it = b1.find(*itLRU);
                        assert(it != b1.end());
                        b1.erase(it);
                        b1_key.remove(*itLRU);
                        PRINTV(logfile << "Case IV evicts b1 " << *itLRU << "** t1 size: "<< t1.size()<< ", t2 size: "<< t2.size() << "** b1 size: "<< b1.size()<< ", b2 size: "<< b2.size() <<endl;);
                    }
                    else {
                        typename key_tracker_type::iterator itLRU = t1_key.begin();
                        assert(itLRU != t1_key.end());
                        typename key_to_value_type::iterator it = t1.find(*itLRU);
                        assert(it != t1.end());
                        t1.erase(it);
                        t1_key.remove(*itLRU);
                        PRINTV(logfile << "Case IV evicts t1 without flushing back to DiskSim input trace " << *itLRU << "** t1 size: "<< t1.size()<< ", t2 size: "<< t2.size() << "** b1 size: "<< b1.size()<< ", b2 size: "<< b2.size() <<endl;);
                    }
                }
            }
            ///ziqi: if it is a read request
            else {
                // Record k as most-recently-used key
                typename key_tracker_type::iterator itNew = t1_key.insert(t1_key.end(), k);
                // Create the key-value entry,
                // linked to the usage record.
                t1.insert(make_pair(k, make_pair(v, itNew)));
                PRINTV(logfile << "Case III insert a clean page to t1: " << k << "** t1 size: "<< t1.size()<< ", t2 size: "<< t2.size() << "** b1 size: "<< b1.size()<< ", b2 size: "<< b2.size() <<endl;);

                while(t1.size() + b1.size() > _capacity) {
                    if(b1.size() > 0) {
                        typename key_tracker_type::iterator itLRU = b1_key.begin();
                        assert(itLRU != b1_key.end());
                        typename key_to_value_type::iterator it = b1.find(*itLRU);
                        assert(it != b1.end());
                        b1.erase(it);
                        b1_key.remove(*itLRU);
                        PRINTV(logfile << "Case III evicts b1 " << *itLRU << "** t1 size: "<< t1.size()<< ", t2 size: "<< t2.size() << "** b1 size: "<< b1.size()<< ", b2 size: "<< b2.size() <<endl;);
                    }
                    else {
                        typename key_tracker_type::iterator itLRU = t1_key.begin();
                        assert(itLRU != t1_key.end());
                        typename key_to_value_type::iterator it = t1.find(*itLRU);
                        assert(it != t1.end());
                        t1.erase(it);
                        t1_key.remove(*itLRU);
                        PRINTV(logfile << "Case III evicts t1 without flushing back to DiskSim input trace " << *itLRU << "** t1 size: "<< t1.size()<< ", t2 size: "<< t2.size() << "** b1 size: "<< b1.size()<< ", b2 size: "<< b2.size() <<endl;);
                    }
                }
            }
            return (status | PAGEMISS);
        }
        ///ziqi: IOCache Case IV: x hit in b2, then enlarge t2, and move x from b2 to t1 if it's a read or to t2 if it's a write
        else if(it_b2 != b2.end()) {
            ///ziqi: delta denotes the step in each ADAPTATION
            int delta;

            PRINTV(logfile << "Case IV Hit on b2: " << k << endl;);

            ///Disk read after a cache miss
            ///ziqi: DiskSim format Request_arrival_time Device_number Block_number Request_size Request_flags
            ///ziqi: Device_number is set to 1. About Request_flags, 0 is for write and 1 is for read
            PRINTV(DISKSIMINPUTSTREAM << setfill(' ')<<left<<fixed<<setw(25)<<value.getReq().issueTime<<left<<setw(8)<<"0"<<left<<fixed<<setw(12)<<k<<left<<fixed<<setw(8)<<"1"<<"1"<<endl;);

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

            REPLACE(k, v, p);

            b2.erase(it_b2);
            b2_key.remove(k);

            PRINTV(logfile << "Case IV evicts b2 " << k << "** t1 size: "<< t1.size()<< ", t2 size: "<< t2.size() << "** b1 size: "<< b1.size()<< ", b2 size: "<< b2.size() <<endl;);

            ///ziqi: if it is a write request
            if(status & WRITE) {
                // Record k as most-recently-used key
                typename key_tracker_type::iterator itNew = t2_key.insert(t2_key.end(), k);
                // Create the key-value entry,
                // linked to the usage record.
                t2.insert(make_pair(k, make_pair(v, itNew)));
                PRINTV(logfile << "Case IV insert a dirty page to t2: " << k << "** t1 size: "<< t1.size()<< ", t2 size: "<< t2.size() << "** b1 size: "<< b1.size()<< ", b2 size: "<< b2.size() <<endl;);
                ///update seqList
                seqListUpdate(k);

                while(t1.size() + b1.size() > _capacity) {
                    if(b1.size() > 0) {
                        typename key_tracker_type::iterator itLRU = b1_key.begin();
                        assert(itLRU != b1_key.end());
                        typename key_to_value_type::iterator it = b1.find(*itLRU);
                        assert(it != b1.end());
                        b1.erase(it);
                        b1_key.remove(*itLRU);
                        PRINTV(logfile << "Case IV evicts b1 " << *itLRU << "** t1 size: "<< t1.size()<< ", t2 size: "<< t2.size() << "** b1 size: "<< b1.size()<< ", b2 size: "<< b2.size() <<endl;);
                    }
                    else {
                        typename key_tracker_type::iterator itLRU = t1_key.begin();
                        assert(itLRU != t1_key.end());
                        typename key_to_value_type::iterator it = t1.find(*itLRU);
                        assert(it != t1.end());
                        t1.erase(it);
                        t1_key.remove(*itLRU);
                        PRINTV(logfile << "Case IV evicts t1 without flushing back to DiskSim input trace " << *itLRU << "** t1 size: "<< t1.size()<< ", t2 size: "<< t2.size() << "** b1 size: "<< b1.size()<< ", b2 size: "<< b2.size() <<endl;);
                    }
                }
            }
            ///ziqi: if it is a read request
            else {
                // Record k as most-recently-used key
                typename key_tracker_type::iterator itNew = t1_key.insert(t1_key.end(), k);
                // Create the key-value entry,
                // linked to the usage record.
                t1.insert(make_pair(k, make_pair(v, itNew)));
                PRINTV(logfile << "Case IV insert a clean page to t1: " << k << "** t1 size: "<< t1.size()<< ", t2 size: "<< t2.size() << "** b1 size: "<< b1.size()<< ", b2 size: "<< b2.size() <<endl;);

                while(t1.size() + b1.size() > _capacity) {
                    if(b1.size() > 0) {
                        typename key_tracker_type::iterator itLRU = b1_key.begin();
                        assert(itLRU != b1_key.end());
                        typename key_to_value_type::iterator it = b1.find(*itLRU);
                        assert(it != b1.end());
                        b1.erase(it);
                        b1_key.remove(*itLRU);
                        PRINTV(logfile << "Case IV evicts b1 " << *itLRU << "** t1 size: "<< t1.size()<< ", t2 size: "<< t2.size() << "** b1 size: "<< b1.size()<< ", b2 size: "<< b2.size() <<endl;);
                    }
                    else {
                        typename key_tracker_type::iterator itLRU = t1_key.begin();
                        assert(itLRU != t1_key.end());
                        typename key_to_value_type::iterator it = t1.find(*itLRU);
                        assert(it != t1.end());
                        t1.erase(it);
                        t1_key.remove(*itLRU);
                        PRINTV(logfile << "Case IV evicts t1 without flushing back to DiskSim input trace " << *itLRU << "** t1 size: "<< t1.size()<< ", t2 size: "<< t2.size() << "** b1 size: "<< b1.size()<< ", b2 size: "<< b2.size() <<endl;);
                    }
                }
            }
            return (status | PAGEMISS);
        }
        ///ziqi: IOCache Case V: x is cache miss
        else {
            PRINTV(logfile << "Case V miss on key: " << k << endl;);

            ///Disk read after a cache miss
            ///ziqi: DiskSim format Request_arrival_time Device_number Block_number Request_size Request_flags
            ///ziqi: Device_number is set to 1. About Request_flags, 0 is for write and 1 is for read
            PRINTV(DISKSIMINPUTSTREAM << setfill(' ')<<left<<fixed<<setw(25)<<value.getReq().issueTime<<left<<setw(8)<<"0"<<left<<fixed<<setw(12)<<k<<left<<fixed<<setw(8)<<"1"<<"1"<<endl;);

            if((t1.size() + b1.size()) == _capacity) {
                if(t1.size() < _capacity) {
                    typename key_tracker_type::iterator itLRU = b1_key.begin();
                    assert(itLRU != b1_key.end());
                    PRINTV(logfile << *itLRU <<endl;);
                    typename key_to_value_type::iterator it = b1.find(*itLRU);
                    assert(it != b1.end());
                    b1.erase(it);
                    b1_key.remove(*itLRU);
                    PRINTV(logfile << "Case V evicts b1 " << *itLRU << "** t1 size: "<< t1.size()<< ", t2 size: "<< t2.size() << "** b1 size: "<< b1.size()<< ", b2 size: "<< b2.size() <<endl;);

                    const V v = _fn(k, value);

                    REPLACE(k, v, p);
                }
                else {
                    typename key_tracker_type::iterator itLRU = t1_key.begin();
                    assert(itLRU != t1_key.end());

                    typename key_to_value_type::iterator it = t1.find(*itLRU);
                    assert(it != t1.end());

                    t1.erase(it);
                    t1_key.remove(*itLRU);
                    PRINTV(logfile << "Case V evicts a clean page without flushing back to DiskSim input trace " << k << "** t1 size: "<< t1.size()<< ", t2 size: "<< t2.size() << "** b1 size: "<< b1.size()<< ", b2 size: "<< b2.size() <<endl;);
                }
            }
            else if((t1.size() + b1.size()) < _capacity) {
                if((t1.size() + t2.size() + b1.size() + b2.size()) >= _capacity) {
                    if((t1.size() + t2.size() + b1.size() + b2.size()) == 2 * _capacity) {
                        typename key_tracker_type::iterator itLRU = b2_key.begin();
                        assert(itLRU != b2_key.end());
                        typename key_to_value_type::iterator it = b2.find(*itLRU);
                        assert(it != b2.end());
                        b2.erase(it);
                        b2_key.remove(*itLRU);
                        PRINTV(logfile << "Case V evicts b2 " << *itLRU << "** t1 size: "<< t1.size()<< ", t2 size: "<< t2.size() << "** b1 size: "<< b1.size()<< ", b2 size: "<< b2.size() <<endl;);
                    }

                    const V v = _fn(k, value);

                    REPLACE(k, v, p);
                }
            }

            const V v = _fn(k, value);

            if(status & WRITE) {
                // Record k as most-recently-used key
                typename key_tracker_type::iterator itNew = t2_key.insert(t2_key.end(), k);
                // Create the key-value entry,
                // linked to the usage record.
                t2.insert(make_pair(k, make_pair(v, itNew)));
                PRINTV(logfile << "Case V insert a dirty page to t2: " << k << "** t1 size: "<< t1.size()<< ", t2 size: "<< t2.size() << "** b1 size: "<< b1.size()<< ", b2 size: "<< b2.size() <<endl;);
                ///update seqList
                seqListUpdate(k);

                while(t1.size() + b1.size() > _capacity) {
                    typename key_tracker_type::iterator itNew = b1_key.begin();
                    assert(itNew != b1_key.end());
                    typename key_to_value_type::iterator it = b1.find(*itNew);
                    assert(it != b1.end());
                    b1.erase(it);
                    b1_key.remove(*itNew);
                    PRINTV(logfile << "Case V evict key from b1 since t1.size() + b1.size() > _capacity: " << *itNew << "** t1 size: "<< t1.size()<< ", t2 size: "<< t2.size() << "** b1 size: "<< b1.size()<< ", b2 size: "<< b2.size() <<endl;);
                }
            }
            else {
                // Record k as most-recently-used key
                typename key_tracker_type::iterator itNew = t1_key.insert(t1_key.end(), k);
                // Create the key-value entry,
                // linked to the usage record.
                t1.insert(make_pair(k, make_pair(v, itNew)));
                PRINTV(logfile << "Case V insert a clean page to t1: " << k << "** t1 size: "<< t1.size()<< ", t2 size: "<< t2.size() << "** b1 size: "<< b1.size()<< ", b2 size: "<< b2.size() <<endl;);

                while(t1.size() + b1.size() > _capacity) {
                    typename key_tracker_type::iterator itNew = b1_key.begin();
                    assert(itNew != b1_key.end());
                    typename key_to_value_type::iterator it = b1.find(*itNew);
                    assert(it != b1.end());
                    b1.erase(it);
                    b1_key.remove(*itNew);
                    PRINTV(logfile << "Case V evict key from b1 since t1.size() + b1.size() > _capacity: " << *itNew << "** t1 size: "<< t1.size()<< ", t2 size: "<< t2.size() << "** b1 size: "<< b1.size()<< ", b2 size: "<< b2.size() <<endl;);
                }
            }

            return (status | PAGEMISS);
        }

        ///should never reach here
        assert(0);
        return 0;

    } //end operator access

    ///ziqi: IOCache subroutine
    void REPLACE(const K &k, const V &v, int p) {
        ///REPLACE can only be triggered only if NVRAM is full
        assert(t1.size() + t2.size() == _capacity);

        typename key_to_value_type::iterator it = b2.find(k);

        if((t1.size() > 0) && ((t1.size() > unsigned(p)) || ((it != b2.end()) && (t1.size() == unsigned(p))))) {
            PRINTV(logfile << "REPLACE evicts from the clean page side" << endl;);
            typename key_tracker_type::iterator itLRU = t1_key.begin();
            assert(itLRU != t1_key.end());
            PRINTV(logfile << *itLRU << endl;);
            typename key_to_value_type::iterator itLRUValue = t1.find(*itLRU);
            assert(itLRUValue != t1.end());
            typename key_tracker_type::iterator itNew = b1_key.insert(b1_key.end(), *itLRU);
            // Create the key-value entry,
            // linked to the usage record.
            const V v_tmp = _fn(*itLRU, itLRUValue->second.first);
            b1.insert(make_pair(*itLRU, make_pair(v_tmp, itNew)));
            PRINTV(logfile << "REPLACE insert key to b1: " << *itLRU << "** t1 size: "<< t1.size()<< ", t2 size: "<< t2.size() << "** b1 size: "<< b1.size()<< ", b2 size: "<< b2.size() <<endl;);
            t1.erase(itLRUValue);
            t1_key.remove(*itLRU);
            PRINTV(logfile << "REPLACE evicting a clean page without flushing back to DiskSim input trace " << *itLRU << "** t1 size: "<< t1.size()<< ", t2 size: "<< t2.size() << "** b1 size: "<< b1.size()<< ", b2 size: "<< b2.size() <<endl;);
        }
        else {
            PRINTV(logfile << "REPLACE evicts from the dirty page side" << endl;);
            ///find the max length in seqList
            ///store the maximum length in seqList
            int maxLength = 0;
            ///store the key of maximum length in seqList
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

            ///if maxLength in the seqList above the threshold, flush the sequence and evict the leading one
            if(maxLength > threshold) {
                PRINTV(logfile << "maxLength is above threshold: " << threshold << endl;);
                ///flush to HDD
                ///ziqi: DiskSim format Request_arrival_time Device_number Block_number Request_size Request_flags
                ///ziqi: Device_number is set to 1. About Request_flags, 0 is for write and 1 is for read
                PRINTV(logfile << "flushing to disksim input file" <<  endl;);
                PRINTV(DISKSIMINPUTSTREAM<<setfill(' ')<<left<<fixed<<setw(25)<<v.getReq().issueTime<<left<<setw(8)<<"0"<<left<<fixed<<setw(12)<<maxLengthKey<<left<<fixed<<setw(8)<<maxLength<<"0"<<endl;);

                ///keep score of total page write number to storage
                totalPageWriteToStorage = totalPageWriteToStorage + maxLength;

                ///insert maxLengthKey to b2
                typename key_to_value_type::iterator itFirst = t2.find(maxLengthKey);
                assert(itFirst != t2.end());
                typename key_tracker_type::iterator itNew = b2_key.insert(b2_key.end(), maxLengthKey);
                const V v_tmp = _fn(maxLengthKey, itFirst->second.first);
                b2.insert(make_pair(maxLengthKey, make_pair(v_tmp, itNew)));
                PRINTV(logfile << "REPLACE insert key to b2: " << maxLengthKey << "** t1 size: "<< t1.size()<< ", t2 size: "<< t2.size() << "** b1 size: "<< b1.size()<< ", b2 size: "<< b2.size() <<endl;);

                ///insert from maxLengthKey+1 to maxLengthKey+maxLength to t1
                int i=1;
                while(i<maxLength) {
                    typename key_to_value_type::iterator it = t2.find(maxLengthKey+i);
                    assert(it != t2.end());
                    ///insert to LRU position instead of MRU position
                    typename key_tracker_type::iterator itNew = t1_key.insert(t1_key.begin(), maxLengthKey+i);
                    V v_tmp = _fn(maxLengthKey+i, it->second.first);
                    t1.insert(make_pair(maxLengthKey+i, make_pair(v_tmp, itNew)));
                    PRINTV(logfile << "REPLACE insert to LRU of t1: " << maxLengthKey+i << "** t1 size: "<< t1.size()<< ", t2 size: "<< t2.size() << "** b1 size: "<< b1.size()<< ", b2 size: "<< b2.size() <<endl;);
                    i++;
                }

                ///evict from maxLengthKey to maxLengthKey+maxLength from t2
                i=0;
                while(i<maxLength) {
                    typename key_to_value_type::iterator it = t2.find(maxLengthKey+i);
                    assert(it != t2.end());
                    t2.erase(it);
                    t2_key.remove(maxLengthKey+i);
                    PRINTV(logfile << "REPLACE erase seq page: " << maxLengthKey+i << "** t1 size: "<< t1.size()<< ", t2 size: "<< t2.size() << "** b1 size: "<< b1.size()<< ", b2 size: "<< b2.size() <<endl;);
                    i++;
                }
                ///erase maxLengthKey from seqList
                seqList.erase(maxLengthKey);
                PRINTV(logfile << "REPLACE erase maxLengthKey key from seqList: " << maxLengthKey << endl;);
            }
            ///maxLength is smaller than threshold, evcit the LRU page in t2
            else {
                PRINTV(logfile << "maxLength is less or equal to threshold: " << threshold << endl;);

                typename key_tracker_type::iterator itLRU = t2_key.begin();
                assert(itLRU != t2_key.end());
                typename key_to_value_type::iterator itLRUValue = t2.find(*itLRU);
                assert(itLRUValue != t2.end());
                ///flush to HDD
                ///ziqi: DiskSim format Request_arrival_time Device_number Block_number Request_size Request_flags
                ///ziqi: Device_number is set to 1. About Request_flags, 0 is for write and 1 is for read
                PRINTV(logfile << "flushing to disksim input file" <<  endl;);
                PRINTV(DISKSIMINPUTSTREAM<<setfill(' ')<<left<<fixed<<setw(25)<<v.getReq().issueTime<<left<<setw(8)<<"0"<<left<<fixed<<setw(12)<<*itLRU<<left<<fixed<<setw(8)<<"1"<<"0"<<endl;);

                ///keep score of total page write number to storage
                totalPageWriteToStorage++;

                ///insert LRU page from t2 to b2
                typename key_tracker_type::iterator itNew = b2_key.insert(b2_key.end(), *itLRU);
                const V v_tmp = _fn(*itLRU, itLRUValue->second.first);
                b2.insert(make_pair(*itLRU, make_pair(v_tmp, itNew)));
                PRINTV(logfile << "REPLACE insert key to b2: " << maxLengthKey << "** t1 size: "<< t1.size()<< ", t2 size: "<< t2.size() << "** b1 size: "<< b1.size()<< ", b2 size: "<< b2.size() <<endl;);

                ///evict LRU page from t2
                t2.erase(itLRUValue);
                t2_key.remove(*itLRU);
                PRINTV(logfile << "REPLACE erase LRU page from t2: " << *itLRU << "** t1 size: "<< t1.size()<< ", t2 size: "<< t2.size() << "** b1 size: "<< b1.size()<< ", b2 size: "<< b2.size() <<endl;);
            }
        }
    }

    ///IOCache subroutine
    void seqListUpdate(const K &k) {
        PRINTV(logfile << "seqListUpdate key " << k <<  endl;);
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

private:

// The function to be cached
    V(*_fn)(const K & , V);
// Maximum number of key-value pairs to be retained
    const size_t _capacity;

    unsigned levelMinusMinus;

    ///ziqi: Key access history
    key_tracker_type t1_key;
    ///ziqi: Key-to-value lookup
    key_to_value_type t1;
    ///ziqi: Key access history
    key_tracker_type t2_key;
    ///ziqi: Key-to-value lookup
    key_to_value_type t2;
    ///ziqi: Key access history
    key_tracker_type b1_key;
    ///ziqi: Key-to-value lookup
    key_to_value_type b1;
    ///ziqi: Key access history
    key_tracker_type b2_key;
    ///ziqi: Key-to-value lookup
    key_to_value_type b2;

    ///ziqi: Key-to-value lookup
    key_to_value_type_seqList seqList;
};

#endif //end lru_stl
