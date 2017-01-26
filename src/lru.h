//
// C++ Interface: lru_pure
//
// Description:
//
//
// Author: Ziqi Fan, (C) 2011
//
// Copyright: See COPYING file that comes with this distribution
//

#ifndef LRU_H
#define LRU_H

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

///extern int dirtyPageInCache;


// Class providing fixed-size (by number of records)
// LRU-replacement cache of a function with signature
// V f(K)
template <typename K, typename V>
class LRU : public TestCache<K, V>
{
public:
// Key access history, most recent at back
    typedef list<K> key_tracker_type;
// Key to value and key history iterator
    typedef map
    < K, pair<V, typename key_tracker_type::iterator> > 	key_to_value_type;

    LRU(
        V(*f)(const K & , V),
        size_t c,
        unsigned levelMinus
    ) : _fn(f) , _capacity(c), levelMinusMinus(levelMinus)  {
        assert ( _capacity!=0 );
    }

    uint32_t access(const K &k  , V &value, uint32_t status) {
        PRINTV(logfile << endl;);
        assert(_key_to_value.size() <= _capacity);
        assert(_capacity != 0);
        PRINTV(logfile << "Access key: " << k << endl;);

        ///ziqi: if request is write, mark the page status as DIRTY
        if(status & WRITE) {
            status |= DIRTY;
            value.updateFlags(status);
        }

        const typename key_to_value_type::iterator it	= _key_to_value.find(k);

        // Case I: cache miss
        if(it == _key_to_value.end()) {
            PRINTV(logfile << "Case I Miss on key: " << k << endl;);

            // Disk read after a cache miss
            // DiskSim format Request_arrival_time Device_number Block_number Request_size Request_flags
            // Device_number is set to 1. About Request_flags, 0 is for write and 1 is for read
            PRINTV(DISKSIMINPUTSTREAM << setfill(' ')<<left<<fixed<<setw(25)<<value.getReq().issueTime<<left<<setw(8)<<"0"<<left<<fixed<<setw(12)<<k<<left<<fixed<<setw(8)<<"1"<<"1"<<endl;);


            ///if cache is full, evict LRU page
            if(_key_to_value.size() == _capacity) {
                PRINTV(logfile << "Cache is Full " << _key_to_value.size() << endl;);
                typename key_tracker_type::iterator itTracker = _key_tracker.begin();
                assert(itTracker != _key_tracker.end());
                assert(!_key_tracker.empty());

                typename key_to_value_type::iterator it = _key_to_value.find(*itTracker);
                assert(it != _key_to_value.end());

                // for a dirty page, flush before evict
                if(it->second.first.getReq().flags & DIRTY) {
                    // DiskSim format Request_arrival_time Device_number Block_number Request_size Request_flags
                    // Device_number is set to 1. About Request_flags, 0 is for write and 1 is for read
                    PRINTV(DISKSIMINPUTSTREAM << setfill(' ')<<left<<fixed<<setw(25)<<value.getReq().issueTime<<left<<setw(8)<<"0"<<left<<fixed<<setw(12)<<*itTracker<<left<<fixed<<setw(8)<<"1"<<"0"<<endl;);

                    // Erase both elements to completely purge record
                    PRINTV(logfile << "evict a dirty page " << *itTracker <<  endl;);
                    totalPageWriteToStorage++;
                    _key_to_value.erase(it);
                    _key_tracker.remove(*itTracker);
                    PRINTV(logfile << "Cache utilization: " << _key_to_value.size() <<"/"<<_capacity <<endl;);
                }
                // for a clean page, just evict
                else {
                    PRINTV(logfile << "evict a clean page " << *itTracker <<  endl;);
                    _key_to_value.erase(it);
                    _key_tracker.remove(*itTracker);
                    PRINTV(logfile << "Cache utilization: " << _key_to_value.size() <<"/"<<_capacity <<endl;);
                }
            }

            // insert page to MRU position
            const V v = _fn(k, value);
            typename key_tracker_type::iterator it = _key_tracker.insert(_key_tracker.end(), k);
            _key_to_value.insert(make_pair(k, make_pair(v, it)));
            PRINTV(logfile << "Insert page to MRU position: " << k << endl;);
            PRINTV(logfile << "Cache utilization: " << _key_to_value.size() <<"/"<<_capacity <<endl;);
            return (status | PAGEMISS);
        }
        // Case II: cache hit
        else {
            PRINTV(logfile << "Case II Hit on key: " << k << endl;);

            // if the requst is a read, then it's a read hit, don't need to distinguish it hits on a dirty page or clean page
            if(value.getFlags() & READ) {
                // if a read hit on a dirty page, preserve the page's dirty status
                value.updateFlags(status | (it->second.first.getReq().flags & DIRTY));

                _key_to_value.erase(it);
                _key_tracker.remove(k);

                const V v = _fn(k, value);
                // Record k as most-recently-used key
                typename key_tracker_type::iterator itNew = _key_tracker.insert(_key_tracker.end(), k);
                // Create the key-value entry,
                // linked to the usage record.
                _key_to_value.insert(make_pair(k, make_pair(v, itNew)));
                PRINTV(logfile << "Insert page to MRU position: " << k << endl;);
                PRINTV(logfile << "Cache utilization: " << _key_to_value.size() <<"/"<<_capacity <<endl;);
            }
            // if the request is a write, then distinguish whether it hits on a clean page or a dirty page
            else {
                if(it->second.first.getReq().flags & DIRTY) {
                    writeHitOnDirty++;
                }
                else {
                    writeHitOnClean++;
                }

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
                PRINTV(logfile << "Insert page to MRU position: " << k << endl;);
                PRINTV(logfile << "Cache utilization: " << _key_to_value.size() <<"/"<<_capacity <<endl;);
            }
            return (status | PAGEHIT | BLKHIT);
        }
    } //end operator access

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

#endif //end lru
