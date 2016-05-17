//
// C++ Interface: lru_stl
//
// Description:
//
//
// Author: ARH,,, <arh@aspire-one>, (C) 2011
//
// Copyright: See COPYING file that comes with this distribution
//

#ifndef LRU_WSR_H
#define LRU_WSR_H

#include <map>
#include <list>
#include "cassert"
#include "iostream"
#include "iomanip"
#include "bitset"
#include "global.h"
#include "baseCache.h"

using namespace std;

///ziqi: in access(), if the value's time stamp is the first one that equal or bigger than a multiple of 30s, then flushing back all the dirty pages in buffer cache.
///Then change the dirty pages status to clean. Log these dirty pages into DiskSim input trace.
///ziqi: in access(), for status is write, add dirty page notation to the status.
///(done) ziqi: in remove(), modify it as lru_ziqi.h. Log the evicted dirty page.

extern int totalPageWriteToStorage;


// Class providing fixed-size (by number of records)
// LRU-replacement cache of a function with signature
// V f(K)
template <typename K, typename V>
class LRUWSRCache : public TestCache<K, V>
{
public:
// Key access history, most recent at back
    typedef list<K> key_tracker_type;
// Key to value and key history iterator
    typedef map
    < K, pair<V, typename key_tracker_type::iterator> > 	key_to_value_type;
// Constuctor specifies the cached function and
// the maximum number of records to be stored.
    LRUWSRCache(
        V(*f)(const K & , V),
        size_t c,
        unsigned levelMinus
    ) : _fn(f) , _capacity(c), levelMinusMinus(levelMinus)  {
        ///ARH: Commented for single level cache implementation
//         assert ( _capacity!=0 );
    }
    // Obtain value of the cached function for k

    uint32_t access(const K &k  , V &value, uint32_t status) {

        PRINTV(logfile <<endl;);

        assert(_key_to_value.size() <= _capacity);
        assert(_capacity != 0);
        PRINTV(logfile << "Access key: " << k << endl;);

///ziqi: if request is write, mark the page status as DIRTY
        if(status & WRITE) {
            status |= DIRTY;
            value.updateFlags(status);
        }

// Attempt to find existing record
        const typename key_to_value_type::iterator it	= _key_to_value.find(k);
        //const typename key_to_value_type::iterator itNew	= _key_to_value.find(k);

        if(it == _key_to_value.end()) {
// We don’t have it:
            PRINTV(logfile << "Miss on key: " << k << endl;);

            ///Disk read after a cache miss
            ///ziqi: DiskSim format Request_arrival_time Device_number Block_number Request_size Request_flags
            ///ziqi: Device_number is set to 1. About Request_flags, 0 is for write and 1 is for read
            PRINTV(DISKSIMINPUTSTREAM << setfill(' ')<<left<<fixed<<setw(25)<<value.getReq().issueTime<<left<<setw(8)<<"0"<<left<<fixed<<setw(12)<<k<<left<<fixed<<setw(8)<<"1"<<"1"<<endl;);

// Evaluate function and create new record
            const V v = _fn(k, value);
///ziqi: inserts new elements on read and write miss
            status |=  insert(k, v);
            PRINTV(logfile << "Insert done on key: " << k << endl;);
            PRINTV(logfile << "Cache utilization: " << _key_to_value.size() <<"/"<<_capacity <<endl;);
            return (status | PAGEMISS);
        }
        else {
            PRINTV(logfile << "Hit on key: " << k << endl;);
// We do have it. Before returning value,
// update access record by moving accessed
// key to back of list.

            ///ziqi: if a read hit on a dirty page, preserve the page's dirty status
            value.updateFlags(status | (it->second.first.getReq().flags & DIRTY));

            ///ziqi: if a hit on a cold dirty page, mark this page to not cold
            value.updateFlags(it->second.first.getReq().flags & ~COLD);

            _key_to_value.erase(it);
            _key_tracker.remove(k);
            assert(_key_to_value.size() < _capacity);
            const V v = _fn(k, value);
            // Record k as most-recently-used key
            typename key_tracker_type::iterator itNew
                = _key_tracker.insert(_key_tracker.end(), k);
            // Create the key-value entry,
            // linked to the usage record.
            _key_to_value.insert(make_pair(k, make_pair(v, itNew)));
            PRINTV(logfile << "Hitted key status: " << bitset<13>(v.getReq().flags) << endl;);
            return (status | PAGEHIT | BLKHIT);
        }
    } //end operator access

private:

// Record a fresh key-value pair in the cache
    int insert(const K &k, const V &v) {
        PRINTV(logfile << "insert key " << k  << endl;);
        PRINTV(logfile << "Key bit status: " << bitset<13>(v.getReq().flags) << endl;);
        int status = 0;
// Method is only called on cache misses
        assert(_key_to_value.find(k) == _key_to_value.end());

// Make space if necessary
        if(_key_to_value.size() == _capacity) {
            PRINTV(logfile << "Cache is Full " << _key_to_value.size() << " sectors" << endl;);
            evict(v);
            status = EVICT;
        }

// Record k as most-recently-used key
        typename key_tracker_type::iterator it
            = _key_tracker.insert(_key_tracker.end(), k);
// Create the key-value entry,
// linked to the usage record.
        _key_to_value.insert(make_pair(k, make_pair(v, it)));
// No need to check return,
// given previous assert.
// 			add_sram_entry(k,false);
        return status;
    }


///ziqi: v is used to denote the original entry that passed to access() method. We only replace the time stamp of evicted entry by the time stamp of v
    // Purge the least-recently-used element in the cache
    void evict(const V &v) {

        ///ziqi: denote whether any page has been evicted. If none is evicted, evict the origianl dirty page
        bool evictSth = false;

        typename key_tracker_type::iterator itTracker;
        typename key_to_value_type::iterator it;

        PRINTV(logfile <<"size of _key_tracker: "<<_key_tracker.size()<<endl;);

        while(true) {

            itTracker = _key_tracker.begin();

            it = _key_to_value.find(*itTracker);
            assert(it != _key_to_value.end());

            PRINTV(logfile << "Considering Key for eviction: " << *(itTracker) << endl;);

            if(it->second.first.getReq().flags & DIRTY) {
                if(it->second.first.getReq().flags & COLD) {
                    ///ziqi: DiskSim format Request_arrival_time Device_number Block_number Request_size Request_flags
                    ///ziqi: Device_number is set to 1. About Request_flags, 0 is for write and 1 is for read
                    PRINTV(DISKSIMINPUTSTREAM << setfill(' ')<<left<<fixed<<setw(25)<<v.getReq().issueTime<<left<<setw(8)<<"0"<<left<<fixed<<setw(12)<<*itTracker<<left<<fixed<<setw(8)<<"1"<<"0"<<endl;);

                    // Erase both elements to completely purge record
                    PRINTV(logfile << "evicting dirty key " << *(itTracker) <<  endl;);
                    totalPageWriteToStorage++;
                    PRINTV(logfile << "Key dirty bit status: " << bitset<13>(it->second.first.getReq().flags) << endl;);
                    it = _key_to_value.find(*itTracker);
                    assert(it != _key_to_value.end());
                    _key_to_value.erase(it);
                    _key_tracker.remove(*(itTracker));

                    evictSth = true;

                    PRINTV(logfile << "Cache utilization: " << _key_to_value.size() <<"/"<<_capacity <<endl;);

                    break;
                }

                ///ziqi: Move the not-cold dirty page to MRU position
                else {
                    PRINTV(logfile << "Key status before: " << bitset<13>(it->second.first.getReq().flags) << endl;);
                    it->second.first.updateFlags((it->second.first.getReq().flags) | COLD);
                    PRINTV(logfile << "Key status after: " << bitset<13>(it->second.first.getReq().flags) << endl;);

                    _key_to_value.erase(it);
                    _key_tracker.remove(*itTracker);
                    assert(_key_to_value.size() < _capacity);
                    const V v = _fn(*(itTracker), it->second.first);
                    // Record k as most-recently-used key
                    typename key_tracker_type::iterator itNew
                        = _key_tracker.insert(_key_tracker.end(), *(itTracker));
                    // Create the key-value entry,
                    // linked to the usage record.
                    _key_to_value.insert(make_pair(*(itTracker), make_pair(v, itNew)));
                    PRINTV(logfile << "Not-cold dirty page at LRU, moved to MRU: " << *(itTracker) << endl;);
                    PRINTV(logfile << "Cache utilization: " << _key_to_value.size() <<"/"<<_capacity <<endl;);
                }
            }
            else {
                PRINTV(logfile << "evicting clean key without flushing back to DiskSim input trace " << *(itTracker) <<  endl;);

                PRINTV(logfile << "Key status: " << bitset<13>(it->second.first.getReq().flags) << endl;);
                it = _key_to_value.find(*itTracker);
                assert(it != _key_to_value.end());

                _key_to_value.erase(it);
                _key_tracker.remove(*(itTracker));

                evictSth = true;

                PRINTV(logfile << "Cache utilization: " << _key_to_value.size() <<"/"<<_capacity <<endl;);

                break;
            }
        }

        if(evictSth == false) {
            itTracker = _key_tracker.begin();

            PRINTV(logfile << "last resort: evicting original dirty key " << *itTracker <<  endl;);
            totalPageWriteToStorage++;
            PRINTV(logfile << "Key status: " << bitset<13>(it->second.first.getReq().flags) << endl;);
            it = _key_to_value.find(*itTracker);
            assert(it != _key_to_value.end());

            ///ziqi: DiskSim format Request_arrival_time Device_number Block_number Request_size Request_flags
            ///ziqi: Device_number is set to 1. About Request_flags, 0 is for write and 1 is for read
            PRINTV(DISKSIMINPUTSTREAM << setfill(' ')<<left<<fixed<<setw(25)<<v.getReq().issueTime<<left<<setw(8)<<"0"<<left<<fixed<<setw(12)<<*itTracker<<left<<fixed<<setw(8)<<"1"<<"0"<<endl;);

            _key_to_value.erase(it);
            _key_tracker.remove(*itTracker);

            PRINTV(logfile << "Cache utilization: " << _key_to_value.size() <<"/"<<_capacity <<endl;);
        }
    }

// The function to be cached
    V(*_fn)(const K & , V);
// Maximum number of key-value pairs to be retained
    const size_t _capacity;

// Key access history
    key_tracker_type _key_tracker;
// Key-to-value lookup
    key_to_value_type _key_to_value;
    unsigned levelMinusMinus;
};

#endif //end lru_stl