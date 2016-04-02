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

#ifndef LARC_H
#define LARC_H

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

extern int totalEvictedCleanPages;

extern int totalNonSeqEvictedDirtyPages;

// Class providing fixed-size (by number of records)
// LRU-replacement cache of a function with signature
// V f(K)
template <typename K, typename V>
class LARC : public TestCache<K, V>
{
public:
// Key access history, most recent at back
    typedef list<K> key_tracker_type;
// Key to value and key history iterator
    typedef map
    < K, pair<V, typename key_tracker_type::iterator> > 	key_to_value_type;
// Constuctor specifies the cached function and
// the maximum number of records to be stored.
    LARC(
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
        ///ziqi: cr denotes the length limit of ghost cache
        static int cr = int(0.1 * _capacity);

        assert(q.size() <= _capacity);
        assert(q.size() >= 0);
        assert(qr.size() >= 0);
        PRINTV(logfile << "Access key: " << k << endl;);

///ziqi: if request is write, mark the page status as DIRTY
        if(status & WRITE) {
            status |= DIRTY;
            value.updateFlags(status);
            //cout<<"flags are "<<value.getFlags()<<endl;
            //const V v1 = _fn(k, value);
            //insertDirtyPage(k, v1);
        }

// Attempt to find existing record
        const typename key_to_value_type::iterator it_q	= q.find(k);
        const typename key_to_value_type::iterator it_qr	= qr.find(k);
        //const typename key_to_value_type::iterator itNew	= _key_to_value.find(k);

        ///ziqi: x hit in q, then move x to MRU of q
        if(it_q != q.end()) {
            assert(it_qr == qr.end());
            PRINTV(logfile << "Hit on q: " << k << endl;);

            value.updateFlags(status | (it_q->second.first.getReq().flags & DIRTY));

            q.erase(it_q);
            q_key.remove(k);
            assert(q.size() < _capacity);
            const V v = _fn(k, value);
            // Record k as most-recently-used key
            typename key_tracker_type::iterator itNew = q_key.insert(q_key.end(), k);
            // Create the key-value entry,
            // linked to the usage record.
            q.insert(make_pair(k, make_pair(v, itNew)));
            PRINTV(logfile << "Insert key to q: " << k << "** q size: "<< q.size()<< ", qr size: "<< qr.size() <<endl;);

            PRINTV(logfile << "ADAPTATION: cr decreases from " << cr << " to ";);
            if( int(0.1 * _capacity) > int(cr - _capacity/(_capacity - cr)) )
                cr = (0.1 * _capacity);
            else
                cr = int(cr - _capacity/(_capacity - cr));
            PRINTV(logfile << cr << endl;);

            return (status | PAGEHIT | BLKHIT);
        }

        PRINTV(logfile << "ADAPTATION: cr increases from " << cr << " to ";);
        if( int(0.9 * _capacity) < int(cr + _capacity/cr) )
            cr = (0.9 * _capacity);
        else
            cr = int(cr + _capacity/- cr);
        PRINTV(logfile << cr << endl;);

        ///ziqi: x hit in qr, then move x from qr to q
        if(it_qr != qr.end()) {

            assert(it_q == q.end());

            PRINTV(logfile << "Hit on qr: " << k << endl;);

            ///value.updateFlags(status | (it_b1->second.first.getReq().flags & DIRTY));

            qr.erase(it_qr);
            qr_key.remove(k);

            const V v = _fn(k, value);

            ///ziqi: if q is full, evict LRU page for the inserted page from qr
            if(q.size() == _capacity) {

                typename key_tracker_type::iterator itLRU = q_key.begin();
                assert(itLRU != q_key.end());
                typename key_to_value_type::iterator it = q.find(*itLRU);

                if(it->second.first.getReq().flags & DIRTY) {
                    ///ziqi: DiskSim format Request_arrival_time Device_number Block_number Request_size Request_flags
                    ///ziqi: Device_number is set to 1. About Request_flags, 0 is for write and 1 is for read
                    PRINTV(DISKSIMINPUTSTREAM << setfill(' ')<<left<<fixed<<setw(25)<<v.getReq().issueTime<<left<<setw(8)<<"0"<<left<<fixed<<setw(12)<<it->second.first.getReq().fsblkno<<left<<fixed<<setw(8)<<it->second.first.getReq().reqSize<<"0"<<endl;);

                    // Erase both elements to completely purge record
                    PRINTV(logfile << "Evicting dirty key from q: " << k <<  endl;);
                    totalNonSeqEvictedDirtyPages++;
                    PRINTV(logfile << "Key dirty bit status: " << bitset<10>(it->second.first.getReq().flags) << endl;);
                    q.erase(it);
                    q_key.remove(*itLRU);

                    PRINTV(logfile << "q size: " << q.size() <<endl;);
                }
                else {
                    PRINTV(logfile << "Evicting clean key without flushing back to DiskSim input trace " << k <<  endl;);
                    totalEvictedCleanPages++;
                    PRINTV(logfile << "Key clean bit status: " << bitset<10>(it->second.first.getReq().flags) << endl;);

                    q.erase(it);
                    q_key.remove(*itLRU);
                    PRINTV(logfile << "q size: " << q.size() <<endl;);
                }
            }

            assert(q.size() < _capacity);
            // Record k as most-recently-used key
            typename key_tracker_type::iterator itNew = q_key.insert(q_key.end(), k);
            // Create the key-value entry,
            // linked to the usage record.
            q.insert(make_pair(k, make_pair(v, itNew)));
            PRINTV(logfile << "Insert key to q: " << k << "** q size: "<< q.size()<< ", qr size: "<< qr.size() <<endl;);

            return (status | PAGEMISS);
        }
        ///ziqi: x not in qr, insert x to qr MRU
        else {

            if(qr.size() >= cr) {
                typename key_tracker_type::iterator itLRU = qr_key.begin();
                assert(itLRU != qr_key.end());
                typename key_to_value_type::iterator it = qr.find(*itLRU);

                qr.erase(it);
                qr_key.remove(*itLRU);
            }

            const V v = _fn(k, value);

            // Record k as most-recently-used key
            typename key_tracker_type::iterator itNew = qr_key.insert(qr_key.end(), k);
            // Create the key-value entry,
            // linked to the usage record.
            qr.insert(make_pair(k, make_pair(v, itNew)));
            PRINTV(logfile << "Insert key to qr: " << k << "** q size: "<< q.size()<< ", qr size: "<< qr.size() <<endl;);
            return (status | PAGEMISS);
        }

        return 0;

    } //end operator access



private:

// The function to be cached
    V(*_fn)(const K & , V);
// Maximum number of key-value pairs to be retained
    const size_t _capacity;

    unsigned levelMinusMinus;

    ///ziqi: Key access history
    key_tracker_type q_key;
    ///ziqi: Key-to-value lookup
    key_to_value_type q;
    ///ziqi: Key access history
    key_tracker_type qr_key;
    ///ziqi: Key-to-value lookup
    key_to_value_type qr;
};

#endif //end lru_stl
