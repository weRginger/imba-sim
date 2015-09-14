//
// C++ Interface: harc (previously called darcer)
//
// Description: Enhanced version of darc. H-ARC includes frequency and recency on top of dirty and clean.
//
// Author: Ziqi Fan, (C) 2014
//
// Copyright: See COPYING file that comes with this distribution
//

#ifndef HARC_H
#define HARC_H

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
class HARC : public TestCache<K, V>
{
public:
// Key access history, most recent at back
    typedef list<K> key_tracker_type;
// Key to value and key history iterator
    typedef map
    < K, pair<V, typename key_tracker_type::iterator> > 	key_to_value_type;
// Constuctor specifies the cached function and
// the maximum number of records to be stored.
    HARC(
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
        ///ziqi: P denotes the length of C and (_capacity - P) denotes the lenght of D
        static int P=0;
        ///ziqi: P_D is the splitter of D_1i and D_2i in fraction
        static double P_D=0;
        ///ziqi: P_C is the splitter of C_1i and C_2i in fraction
        static double P_C=0;

        assert((D_1i.size() + D_2i.size() + C_1i.size() + C_2i.size()) <= _capacity);
        assert((D_1i.size() + D_2i.size() + C_1i.size() + C_2i.size() + D_1o.size() + D_2o.size() + C_1o.size() + C_2o.size()) <= 2*_capacity);
        assert((C_1i.size() + C_2i.size() + C_1o.size() + C_2o.size()) <= _capacity);
        assert((D_1i.size() + D_2i.size() + D_1o.size() + D_2o.size()) <= 2*_capacity);
        assert(_capacity != 0);
        PRINTV(logfile << "Access key: " << k << endl;);

///ziqi: if request is write, mark the page status as DIRTY
        if(status & WRITE) {
            status |= DIRTY;
            value.updateFlags(status);
        }

// Attempt to find existing record
        const typename key_to_value_type::iterator it_D_1i	= D_1i.find(k);
        const typename key_to_value_type::iterator it_D_2i	= D_2i.find(k);
        const typename key_to_value_type::iterator it_D_1o	= D_1o.find(k);
        const typename key_to_value_type::iterator it_D_2o	= D_2o.find(k);

        const typename key_to_value_type::iterator it_C_1i	= C_1i.find(k);
        const typename key_to_value_type::iterator it_C_2i	= C_2i.find(k);
        const typename key_to_value_type::iterator it_C_1o	= C_1o.find(k);
        const typename key_to_value_type::iterator it_C_2o	= C_2o.find(k);
        //const typename key_to_value_type::iterator itNew	= _key_to_value.find(k);

        ///ziqi: DARCER Case I: x hit in C_1i or C_2i, then move x to C_2i if it's a read, or to D_2i if it's a write
        if(it_C_1i != C_1i.end()) {
            ///ziqi: if it is a write request
            if(status & WRITE) {
                PRINTV(logfile << "Case I Write hit on C_1i " << k << endl;);

                C_1i.erase(it_C_1i);
                C_1i_key.remove(k);
                assert(C_1i.size() < _capacity);
                const V v = _fn(k, value);
                // Record k as most-recently-used key
                typename key_tracker_type::iterator itNew
                    = D_2i_key.insert(D_2i_key.end(), k);
                // Create the key-value entry,
                // linked to the usage record.
                D_2i.insert(make_pair(k, make_pair(v, itNew)));
                PRINTV(logfile << "Case I insert dirty key to D_2i: " << k << "** C_1i size: "<< C_1i.size()<< ", C_2i size: "<< C_2i.size() <<", D_1i size: "<< D_1i.size() <<", D_2i size: "<< D_2i.size() <<endl;);
                return (status | PAGEHIT | BLKHIT);
            }
            ///ziqi: if it is a read request
            else {
                PRINTV(logfile << "Case I Read hit on C_1i " << k << endl;);

                C_1i.erase(it_C_1i);
                C_1i_key.remove(k);
                assert(C_1i.size() < _capacity);
                const V v = _fn(k, value);
                // Record k as most-recently-used key
                typename key_tracker_type::iterator itNew
                    = C_2i_key.insert(C_2i_key.end(), k);
                // Create the key-value entry,
                // linked to the usage record.
                C_2i.insert(make_pair(k, make_pair(v, itNew)));

                PRINTV(logfile << "Case I insert clean key to C_2i: " << k << "** C_1i size: "<< C_1i.size()<< ", C_2i size: "<< C_2i.size() <<", D_1i size: "<< D_1i.size() <<", D_2i size: "<< D_2i.size() <<endl;);

                return (status | PAGEHIT | BLKHIT);
            }
        }
        else if(it_C_2i != C_2i.end()) {
            ///ziqi: if it is a write request
            if(status & WRITE) {
                PRINTV(logfile << "Case I Write hit on C_2i " << k << endl;);

                C_2i.erase(it_C_2i);
                C_2i_key.remove(k);
                assert(C_2i.size() < _capacity);
                const V v = _fn(k, value);
                // Record k as most-recently-used key
                typename key_tracker_type::iterator itNew
                    = D_2i_key.insert(D_2i_key.end(), k);
                // Create the key-value entry,
                // linked to the usage record.
                D_2i.insert(make_pair(k, make_pair(v, itNew)));
                PRINTV(logfile << "Case I insert dirty key to D_2i: " << k << "** C_1i size: "<< C_1i.size()<< ", C_2i size: "<< C_2i.size() <<", D_1i size: "<< D_1i.size() <<", D_2i size: "<< D_2i.size() <<endl;);
                return (status | PAGEHIT | BLKHIT);
            }
            ///ziqi: if it is a read request
            else {
                PRINTV(logfile << "Case I Read hit on C_2i " << k << endl;);

                C_2i.erase(it_C_2i);
                C_2i_key.remove(k);
                assert(C_2i.size() < _capacity);
                const V v = _fn(k, value);
                // Record k as most-recently-used key
                typename key_tracker_type::iterator itNew
                    = C_2i_key.insert(C_2i_key.end(), k);
                // Create the key-value entry,
                // linked to the usage record.
                C_2i.insert(make_pair(k, make_pair(v, itNew)));
                PRINTV(logfile << "Case I insert clean key to C_2i: " << k << "** C_1i size: "<< C_1i.size()<< ", C_2i size: "<< C_2i.size() <<", D_1i size: "<< D_1i.size() <<", D_2i size: "<< D_2i.size() <<endl;);
                return (status | PAGEHIT | BLKHIT);
            }
        }

        ///ziqi: DARCER Case II: x hit in D_1i or D_2i, then move x to MRU of D_2i
        else if(it_D_1i != D_1i.end()) {
            PRINTV(logfile << "Case II hit on D_1i " << k << endl;);

            ///ziqi: if a read hit on a dirty page, preserve the page's dirty status
            value.updateFlags(status | (it_D_1i->second.first.getReq().flags & DIRTY));

            D_1i.erase(it_D_1i);
            D_1i_key.remove(k);
            assert(D_1i.size() < _capacity);
            const V v = _fn(k, value);
            // Record k as most-recently-used key
            typename key_tracker_type::iterator itNew = D_2i_key.insert(D_2i_key.end(), k);
            // Create the key-value entry,
            // linked to the usage record.
            D_2i.insert(make_pair(k, make_pair(v, itNew)));
            PRINTV(logfile << "Case II insert key to D_2i: " << k << "** C_1i size: "<< C_1i.size()<< ", C_2i size: "<< C_2i.size() <<", D_1i size: "<< D_1i.size() <<", D_2i size: "<< D_2i.size() <<endl;);
            return (status | PAGEHIT | BLKHIT);

        }
        else if(it_D_2i != D_2i.end()) {
            PRINTV(logfile << "Case II hit on D_2i " << k << endl;);

            ///ziqi: if a read hit on a dirty page, preserve the page's dirty status
            value.updateFlags(status | (it_D_2i->second.first.getReq().flags & DIRTY));

            D_2i.erase(it_D_2i);
            D_2i_key.remove(k);
            assert(D_2i.size() < _capacity);
            const V v = _fn(k, value);
            // Record k as most-recently-used key
            typename key_tracker_type::iterator itNew = D_2i_key.insert(D_2i_key.end(), k);
            // Create the key-value entry,
            // linked to the usage record.
            D_2i.insert(make_pair(k, make_pair(v, itNew)));
            PRINTV(logfile << "Case II insert key to D_2i: " << k << "** C_1i size: "<< C_1i.size()<< ", C_2i size: "<< C_2i.size() <<", D_1i size: "<< D_1i.size() <<", D_2i size: "<< D_2i.size() <<endl;);
            return (status | PAGEHIT | BLKHIT);
        }


        ///ziqi: DARCER Case III: x hit in C_1o
        else if(it_C_1o != C_1o.end()) {
            ///ziqi: delta denotes the step in each ADAPTATION of P
            int delta;
            ///ziqi: delta denotes the step in each ADAPTATION of P_C
            int delta_C;

            PRINTV(logfile << "Case III Hit on C_1o: " << k << endl;);
            //if(b1.size() >= b2.size())
            delta = 1;
            //else
            //delta = int(b2.size()/b1.size());

            if(C_1o.size() >= C_2o.size())
                delta_C = 1;
            else
                delta_C = int(C_2o.size()/C_1o.size());

            PRINTV(logfile << "ADAPTATION: P increases from " << P << " to ";);
            if((P+delta) > int(_capacity))
                P = _capacity;
            else
                P = P+delta;
            PRINTV(logfile << P << endl;);

            PRINTV(logfile << "ADAPTATION: P_C increases from " << P_C << " to ";);
            if((P_C + (double(delta_C)/double(P))) > 1)
                P_C = 1;
            else
                P_C = P_C + (double(delta_C)/double(P));
            PRINTV(logfile << P_C << endl;);

            const V v = _fn(k, value);

            REPLACE(k, v, P, P_D, P_C);

            C_1o.erase(it_C_1o);
            C_1o_key.remove(k);

            PRINTV(logfile << "Case III evicting C_1o " << k <<  endl;);

            if(status & DIRTY) {
                // Record k as most-recently-used key
                typename key_tracker_type::iterator itNew = D_2i_key.insert(D_2i_key.end(), k);
                // Create the key-value entry,
                // linked to the usage record.
                D_2i.insert(make_pair(k, make_pair(v, itNew)));
                PRINTV(logfile << "Case III insert key to D_2i: " << k << "** C_1i size: "<< C_1i.size()<< ", C_2i size: "<< C_2i.size() <<", D_1i size: "<< D_1i.size() <<", D_2i size: "<< D_2i.size() <<endl;);
            }
            else {
                // Record k as most-recently-used key
                typename key_tracker_type::iterator itNew = C_2i_key.insert(C_2i_key.end(), k);
                // Create the key-value entry,
                // linked to the usage record.
                C_2i.insert(make_pair(k, make_pair(v, itNew)));
                PRINTV(logfile << "Case III insert key to C_2i: " << k << "** C_1i size: "<< C_1i.size()<< ", C_2i size: "<< C_2i.size() <<", D_1i size: "<< D_1i.size() <<", D_2i size: "<< D_2i.size() <<endl;);

                if((C_1i.size() + C_2i.size() + C_1o.size() + C_2o.size()) > _capacity) {
                    if(C_1o.size() > 0) {
                        typename key_tracker_type::iterator itLRU = C_1o_key.begin();
                        assert(itLRU != C_1o_key.end());
                        typename key_to_value_type::iterator it = C_1o.find(*itLRU);
                        C_1o.erase(it);
                        C_1o_key.remove(*itLRU);
                        PRINTV(logfile << "Case III evicting C_1o " << *itLRU <<  endl;);
                    }
                    else if(C_2o.size() > 0) {
                        typename key_tracker_type::iterator itLRU = C_2o_key.begin();
                        assert(itLRU != C_2o_key.end());
                        typename key_to_value_type::iterator it = C_2o.find(*itLRU);
                        C_2o.erase(it);
                        C_2o_key.remove(*itLRU);
                        PRINTV(logfile << "Case III evicting C_2o " << *itLRU <<  endl;);
                    }
                    else if(C_1i.size() > 0) {
                        typename key_tracker_type::iterator itLRU = C_1i_key.begin();
                        assert(itLRU != C_1i_key.end());
                        typename key_to_value_type::iterator it = C_1i.find(*itLRU);
                        C_1i.erase(it);
                        C_1i_key.remove(*itLRU);

                        PRINTV(logfile << "Case III evicting C_1i without flushing back to DiskSim input trace " << *itLRU <<  endl;);
                    }
                    else if(C_2i.size() > 0) {
                        typename key_tracker_type::iterator itLRU = C_2i_key.begin();
                        assert(itLRU != C_2i_key.end());
                        typename key_to_value_type::iterator it = C_2i.find(*itLRU);
                        C_2i.erase(it);
                        C_2i_key.remove(*itLRU);

                        PRINTV(logfile << "Case III evicting C_2i without flushing back to DiskSim input trace " << *itLRU <<  endl;);
                    }
                }

            }
            return (status | PAGEMISS);
        }

        ///ziqi: DARCER Case IV: x hit in C_2o
        else if(it_C_2o != C_2o.end()) {
            ///ziqi: delta denotes the step in each ADAPTATION of P
            int delta;
            ///ziqi: delta denotes the step in each ADAPTATION of P_C
            int delta_C;

            PRINTV(logfile << "Case IV Hit on C_2o: " << k << endl;);

            //if(b1.size() >= b2.size())
            delta = 1;
            //else
            //delta = int(b2.size()/b1.size());

            if(C_2o.size() >= C_1o.size())
                delta_C = 1;
            else
                delta_C = int(C_1o.size()/C_2o.size());

            PRINTV(logfile << "ADAPTATION: P increases from " << P << " to ";);
            if((P+delta) > int(_capacity))
                P = _capacity;
            else
                P = P+delta;
            PRINTV(logfile << P << endl;);

            PRINTV(logfile << "ADAPTATION: P_C decreases from " << P_C << " to ";);
            if((P_C - (double(delta_C)/double(P))) < 0)
                P_C = 0;
            else
                P_C = P_C - (double(delta_C)/double(P));
            PRINTV(logfile << P_C << endl;);

            const V v = _fn(k, value);

            REPLACE(k, v, P, P_D, P_C);

            C_2o.erase(it_C_2o);
            C_2o_key.remove(k);

            PRINTV(logfile << "Case IV evicting C_2o " << k <<  endl;);

            if(status & DIRTY) {
                // Record k as most-recently-used key
                typename key_tracker_type::iterator itNew = D_2i_key.insert(D_2i_key.end(), k);
                // Create the key-value entry,
                // linked to the usage record.
                D_2i.insert(make_pair(k, make_pair(v, itNew)));
                PRINTV(logfile << "Case IV insert key to D_2i: " << k << "** C_1i size: "<< C_1i.size()<< ", C_2i size: "<< C_2i.size() <<", D_1i size: "<< D_1i.size() <<", D_2i size: "<< D_2i.size() <<endl;);
            }
            else {
                // Record k as most-recently-used key
                typename key_tracker_type::iterator itNew = C_2i_key.insert(C_2i_key.end(), k);
                // Create the key-value entry,
                // linked to the usage record.
                C_2i.insert(make_pair(k, make_pair(v, itNew)));
                PRINTV(logfile << "Case IV insert key to C_2i: " << k << "** C_1i size: "<< C_1i.size()<< ", C_2i size: "<< C_2i.size() <<", D_1i size: "<< D_1i.size() <<", D_2i size: "<< D_2i.size() <<endl;);

                if((C_1i.size() + C_2i.size() + C_1o.size() + C_2o.size()) > _capacity) {
                    if(C_1o.size() > 0) {
                        typename key_tracker_type::iterator itLRU = C_1o_key.begin();
                        assert(itLRU != C_1o_key.end());
                        typename key_to_value_type::iterator it = C_1o.find(*itLRU);
                        C_1o.erase(it);
                        C_1o_key.remove(*itLRU);
                        PRINTV(logfile << "Case IV evicting C_1o " << *itLRU <<  endl;);
                    }
                    else if(C_2o.size() > 0) {
                        typename key_tracker_type::iterator itLRU = C_2o_key.begin();
                        assert(itLRU != C_2o_key.end());
                        typename key_to_value_type::iterator it = C_2o.find(*itLRU);
                        C_2o.erase(it);
                        C_2o_key.remove(*itLRU);
                        PRINTV(logfile << "Case IV evicting C_2o " << *itLRU <<  endl;);
                    }
                    else if(C_1i.size() > 0) {
                        typename key_tracker_type::iterator itLRU = C_1i_key.begin();
                        assert(itLRU != C_1i_key.end());
                        typename key_to_value_type::iterator it = C_1i.find(*itLRU);
                        C_1i.erase(it);
                        C_1i_key.remove(*itLRU);

                        PRINTV(logfile << "Case IV evicting C_1i without flushing back to DiskSim input trace " << *itLRU <<  endl;);
                    }
                    else if(C_2i.size() > 0) {
                        typename key_tracker_type::iterator itLRU = C_2i_key.begin();
                        assert(itLRU != C_2i_key.end());
                        typename key_to_value_type::iterator it = C_2i.find(*itLRU);
                        C_2i.erase(it);
                        C_2i_key.remove(*itLRU);

                        PRINTV(logfile << "Case IV evicting C_2i without flushing back to DiskSim input trace " << *itLRU <<  endl;);
                    }
                }
            }
            return (status | PAGEMISS);
        }

        ///ziqi: DARCER Case V: x hit in D_1o
        else if(it_D_1o != D_1o.end()) {
            ///ziqi: delta denotes the step in each ADAPTATION of P
            int delta;
            ///ziqi: delta denotes the step in each ADAPTATION of P_D
            int delta_D;

            PRINTV(logfile << "Case V Hit on D_1o: " << k << endl;);
            if((D_1o.size()+D_2o.size()) >= (C_1o.size()+C_2o.size()))
                delta = 2;
            else
                delta = int(2*(C_1o.size()+C_2o.size())/(D_1o.size()+D_2o.size()));

            if(D_1o.size() >= D_2o.size())
                delta_D = 1;
            else
                delta_D = int(D_2o.size()/D_1o.size());

            PRINTV(logfile << "ADAPTATION: P decreases from " << P << " to ";);
            if((P-delta) < 0)
                P = 0;
            else
                P = P-delta;
            PRINTV(logfile << P << endl;);

            PRINTV(logfile << "ADAPTATION: P_D increases from " << P_D << " to ";);
            if((P_D + (double(delta_D)/((double)_capacity-double(P)))) > 1)
                P_D = 1;
            else
                P_D = P_D + (double(delta_D)/((double)_capacity-double(P)));
            PRINTV(logfile << P_D << endl;);

            const V v = _fn(k, value);

            REPLACE(k, v, P, P_D, P_C);

            D_1o.erase(it_D_1o);
            D_1o_key.remove(k);

            PRINTV(logfile << "Case V evicting D_1o " << k <<  endl;);

            if(status & DIRTY) {
                // Record k as most-recently-used key
                typename key_tracker_type::iterator itNew = D_2i_key.insert(D_2i_key.end(), k);
                // Create the key-value entry,
                // linked to the usage record.
                D_2i.insert(make_pair(k, make_pair(v, itNew)));
                PRINTV(logfile << "Case V insert key to D_2i: " << k << "** C_1i size: "<< C_1i.size()<< ", C_2i size: "<< C_2i.size() <<", D_1i size: "<< D_1i.size() <<", D_2i size: "<< D_2i.size() <<endl;);
            }
            else {
                // Record k as most-recently-used key
                typename key_tracker_type::iterator itNew = C_2i_key.insert(C_2i_key.end(), k);
                // Create the key-value entry,
                // linked to the usage record.
                C_2i.insert(make_pair(k, make_pair(v, itNew)));
                PRINTV(logfile << "Case V insert key to C_2i: " << k << "** C_1i size: "<< C_1i.size()<< ", C_2i size: "<< C_2i.size() <<", D_1i size: "<< D_1i.size() <<", D_2i size: "<< D_2i.size() <<endl;);

                if((C_1i.size() + C_2i.size() + C_1o.size() + C_2o.size()) > _capacity) {
                    if(C_1o.size() > 0) {
                        typename key_tracker_type::iterator itLRU = C_1o_key.begin();
                        assert(itLRU != C_1o_key.end());
                        typename key_to_value_type::iterator it = C_1o.find(*itLRU);
                        C_1o.erase(it);
                        C_1o_key.remove(*itLRU);
                        PRINTV(logfile << "Case V evicting C_1o " << *itLRU <<  endl;);
                    }
                    else if(C_2o.size() > 0) {
                        typename key_tracker_type::iterator itLRU = C_2o_key.begin();
                        assert(itLRU != C_2o_key.end());
                        typename key_to_value_type::iterator it = C_2o.find(*itLRU);
                        C_2o.erase(it);
                        C_2o_key.remove(*itLRU);
                        PRINTV(logfile << "Case V evicting C_2o " << *itLRU <<  endl;);
                    }
                    else if(C_1i.size() > 0) {
                        typename key_tracker_type::iterator itLRU = C_1i_key.begin();
                        assert(itLRU != C_1i_key.end());
                        typename key_to_value_type::iterator it = C_1i.find(*itLRU);
                        C_1i.erase(it);
                        C_1i_key.remove(*itLRU);
                        PRINTV(logfile << "Case V evicting C_1i without flushing back to DiskSim input trace " << *itLRU <<  endl;);
                    }
                    else if(C_2i.size() > 0) {
                        typename key_tracker_type::iterator itLRU = C_2i_key.begin();
                        assert(itLRU != C_2i_key.end());
                        typename key_to_value_type::iterator it = C_2i.find(*itLRU);
                        C_2i.erase(it);
                        C_2i_key.remove(*itLRU);
                        PRINTV(logfile << "Case V evicting C_2i without flushing back to DiskSim input trace " << *itLRU <<  endl;);
                    }
                }
            }
            return (status | PAGEMISS);
        }

        ///ziqi: DARCER Case VI: x hit in D_2o
        else if(it_D_2o != D_2o.end()) {
            ///ziqi: delta denotes the step in each ADAPTATION of P
            int delta;
            ///ziqi: delta denotes the step in each ADAPTATION of P_D
            int delta_D;

            PRINTV(logfile << "Case VI Hit on D_2o: " << k << endl;);
            if((D_1o.size()+D_2o.size()) >= (C_1o.size()+C_2o.size()))
                delta = 2;
            else
                delta = int(2*(C_1o.size()+C_2o.size())/(D_1o.size()+D_2o.size()));

            if(D_2o.size() >= D_1o.size())
                delta_D = 1;
            else
                delta_D = int(D_1o.size()/D_2o.size());

            PRINTV(logfile << "ADAPTATION: P decreases from " << P << " to ";);
            if((P-delta) < 0)
                P = 0;
            else
                P = P-delta;
            PRINTV(logfile << P << endl;);

            PRINTV(logfile << "ADAPTATION: P_D decreases from " << P_D << " to ";);
            if((P_D - (double(delta_D)/((double)_capacity-double(P)))) < 0)
                P_D = 0;
            else
                P_D = P_D - (double(delta_D)/((double)_capacity-double(P)));
            PRINTV(logfile << P_D << endl;);

            const V v = _fn(k, value);

            REPLACE(k, v, P, P_D, P_C);

            D_2o.erase(it_D_2o);
            D_2o_key.remove(k);

            PRINTV(logfile << "Case VI evicting D_2o " << k <<  endl;);

            if(status & DIRTY) {
                // Record k as most-recently-used key
                typename key_tracker_type::iterator itNew = D_2i_key.insert(D_2i_key.end(), k);
                // Create the key-value entry,
                // linked to the usage record.
                D_2i.insert(make_pair(k, make_pair(v, itNew)));
                PRINTV(logfile << "Case VI insert key to D_2i: " << k << "** C_1i size: "<< C_1i.size()<< ", C_2i size: "<< C_2i.size() <<", D_1i size: "<< D_1i.size() <<", D_2i size: "<< D_2i.size() <<endl;);
            }
            else {
                // Record k as most-recently-used key
                typename key_tracker_type::iterator itNew = C_2i_key.insert(C_2i_key.end(), k);
                // Create the key-value entry,
                // linked to the usage record.
                C_2i.insert(make_pair(k, make_pair(v, itNew)));
                PRINTV(logfile << "Case VI insert key to C_2i: " << k << "** C_1i size: "<< C_1i.size()<< ", C_2i size: "<< C_2i.size() <<", D_1i size: "<< D_1i.size() <<", D_2i size: "<< D_2i.size() <<endl;);

                if((C_1i.size() + C_2i.size() + C_1o.size() + C_2o.size()) > _capacity) {
                    if(C_1o.size() > 0) {
                        typename key_tracker_type::iterator itLRU = C_1o_key.begin();
                        assert(itLRU != C_1o_key.end());
                        typename key_to_value_type::iterator it = C_1o.find(*itLRU);
                        C_1o.erase(it);
                        C_1o_key.remove(*itLRU);
                        PRINTV(logfile << "Case VI evicting C_1o " << *itLRU <<  endl;);
                    }
                    else if(C_2o.size() > 0) {
                        typename key_tracker_type::iterator itLRU = C_2o_key.begin();
                        assert(itLRU != C_2o_key.end());
                        typename key_to_value_type::iterator it = C_2o.find(*itLRU);
                        C_2o.erase(it);
                        C_2o_key.remove(*itLRU);
                        PRINTV(logfile << "Case VI evicting C_2o " << *itLRU <<  endl;);
                    }
                    else if(C_1i.size() > 0) {
                        typename key_tracker_type::iterator itLRU = C_1i_key.begin();
                        assert(itLRU != C_1i_key.end());
                        typename key_to_value_type::iterator it = C_1i.find(*itLRU);
                        C_1i.erase(it);
                        C_1i_key.remove(*itLRU);
                        PRINTV(logfile << "Case VI evicting C_1i without flushing back to DiskSim input trace " << *itLRU <<  endl;);
                    }
                    else if(C_2i.size() > 0) {
                        typename key_tracker_type::iterator itLRU = C_2i_key.begin();
                        assert(itLRU != C_2i_key.end());
                        typename key_to_value_type::iterator it = C_2i.find(*itLRU);
                        C_2i.erase(it);
                        C_2i_key.remove(*itLRU);
                        PRINTV(logfile << "Case VI evicting C_2i without flushing back to DiskSim input trace " << *itLRU <<  endl;);
                    }
                }
            }
            return (status | PAGEMISS);
        }

        ///ziqi: HARC Case VII: x is cache miss
        else {
            PRINTV(logfile << "Case VII miss on key: " << k << endl;);

            ///afterCacheTrace
            PRINTV(AFTERCACHETRACE <<value.getFsblkno()<<"R"<<endl;);
            ///afterCacheTrace

            ///ziqi: Case A
            if((C_1i.size() + C_2i.size() + C_1o.size() + C_2o.size()) == _capacity) {
                if((C_1i.size() + C_2i.size()) < _capacity) {
                    if(C_1o.size() > 0) {
                        typename key_tracker_type::iterator itLRU = C_1o_key.begin();
                        assert(itLRU != C_1o_key.end());
                        typename key_to_value_type::iterator it = C_1o.find(*itLRU);
                        C_1o.erase(it);
                        C_1o_key.remove(*itLRU);

                        const V v = _fn(k, value);

                        REPLACE(k, v, P, P_D, P_C);
                    }
                    else {
                        typename key_tracker_type::iterator itLRU = C_2o_key.begin();
                        assert(itLRU != C_2o_key.end());
                        typename key_to_value_type::iterator it = C_2o.find(*itLRU);
                        C_2o.erase(it);
                        C_2o_key.remove(*itLRU);

                        const V v = _fn(k, value);

                        REPLACE(k, v, P, P_D, P_C);
                    }
                }
                else if(C_1i.size() > 0) {
                    typename key_tracker_type::iterator itLRU = C_1i_key.begin();
                    assert(itLRU != C_1i_key.end());

                    typename key_to_value_type::iterator it = C_1i.find(*itLRU);

                    PRINTV(logfile << "Case VII evicting clean key without flushing back to DiskSim input trace " << *itLRU <<  endl;);
                    PRINTV(logfile << "Case VII Key clean bit status: " << bitset<10>(it->second.first.getReq().flags) << endl;);

                    C_1i.erase(it);
                    C_1i_key.remove(*itLRU);
                }
                else {
                    typename key_tracker_type::iterator itLRU = C_2i_key.begin();
                    assert(itLRU != C_2i_key.end());

                    typename key_to_value_type::iterator it = C_2i.find(*itLRU);


                    PRINTV(logfile << "Case VII evicting clean key without flushing back to DiskSim input trace " << *itLRU <<  endl;);
                    PRINTV(logfile << "Case VII Key clean bit status: " << bitset<10>(it->second.first.getReq().flags) << endl;);

                    C_2i.erase(it);
                    C_2i_key.remove(*itLRU);
                }
            }
            ///ziqi: Case B
            else if((C_1i.size() + C_2i.size() + C_1o.size() + C_2o.size()) < _capacity) {
                if((D_1i.size() + D_2i.size() + C_1i.size() + C_2i.size() + D_1o.size() + D_2o.size() + C_1o.size() + C_2o.size()) >= _capacity) {
                    if((D_1i.size() + D_2i.size() + C_1i.size() + C_2i.size() + D_1o.size() + D_2o.size() + C_1o.size() + C_2o.size()) == 2 * _capacity) {
                        if(D_1o.size() > 0) {
                            typename key_tracker_type::iterator itLRU = D_1o_key.begin();
                            assert(itLRU != D_1o_key.end());
                            typename key_to_value_type::iterator it = D_1o.find(*itLRU);
                            D_1o.erase(it);
                            D_1o_key.remove(*itLRU);
                        }
                        else {
                            typename key_tracker_type::iterator itLRU = D_2o_key.begin();
                            assert(itLRU != D_2o_key.end());
                            typename key_to_value_type::iterator it = D_2o.find(*itLRU);
                            D_2o.erase(it);
                            D_2o_key.remove(*itLRU);
                        }
                    }

                    const V v = _fn(k, value);

                    REPLACE(k, v, P, P_D, P_C);
                }
            }

            const V v = _fn(k, value);

            if(status & DIRTY) {
                // Record k as most-recently-used key
                typename key_tracker_type::iterator itNew = D_1i_key.insert(D_1i_key.end(), k);
                // Create the key-value entry,
                // linked to the usage record.
                D_1i.insert(make_pair(k, make_pair(v, itNew)));
                PRINTV(logfile << "Case VII insert key to D_1i: " << k << "** C_1i size: "<< C_1i.size()<< ", C_2i size: "<< C_2i.size() <<", D_1i size: "<< D_1i.size() <<", D_2i size: "<< D_2i.size() <<endl;);
            }
            else {
                // Record k as most-recently-used key
                typename key_tracker_type::iterator itNew = C_1i_key.insert(C_1i_key.end(), k);
                // Create the key-value entry,
                // linked to the usage record.
                C_1i.insert(make_pair(k, make_pair(v, itNew)));
                PRINTV(logfile << "Case VII insert key to C_1i: " << k << "** C_1i size: "<< C_1i.size()<< ", C_2i size: "<< C_2i.size() <<", D_1i size: "<< D_1i.size() <<", D_2i size: "<< D_2i.size() <<endl;);
            }

            return (status | PAGEMISS);
        }

        return 0;

    } //end operator access

    ///ziqi: HARC subroutine
    void REPLACE(const K &k, const V &v, int P, double P_D, double P_C) {
        typename key_to_value_type::iterator it_D_1o = D_1o.find(k);
        typename key_to_value_type::iterator it_D_2o = D_2o.find(k);

        if( ((C_1i.size() + C_2i.size()) > 0) && (((C_1i.size() + C_2i.size()) > unsigned(P)) || ( ((it_D_1o != D_1o.end()) || ((it_D_2o != D_2o.end()))) && ((C_1i.size() + C_2i.size()) == unsigned(P))))) {
            if( (C_1i.size() > unsigned(0)) && ((C_1i.size() >= unsigned(P_C*P))) ) {
                typename key_tracker_type::iterator itLRU = C_1i_key.begin();
                assert(itLRU != C_1i_key.end());
                typename key_to_value_type::iterator itLRUValue = C_1i.find(*itLRU);

                typename key_tracker_type::iterator itNew = C_1o_key.insert(C_1o_key.end(), *itLRU);
                // Create the key-value entry,
                // linked to the usage record.
                const V v_tmp = _fn(*itLRU, itLRUValue->second.first);
                C_1o.insert(make_pair(*itLRU, make_pair(v_tmp, itNew)));

                PRINTV(logfile << "REPLACE insert key to b1: " << *itLRU <<  "** b1 size: "<< C_1o.size() << endl;);

                PRINTV(logfile << "REPLACE evicting clean key without flushing back to DiskSim input trace " << *itLRU <<  endl;);

                PRINTV(logfile << "REPLACE Key clean bit status: " << bitset<10>(itLRUValue->second.first.getReq().flags) << endl;);

                C_1i.erase(itLRUValue);
                C_1i_key.remove(*itLRU);
                PRINTV(logfile << "REPLACE C_1i size: " << C_1i.size() <<endl;);

                PRINTV(logfile << "REPLACE insert key to C_1o: " << k <<  "** C_1o size: "<< C_1o.size() << endl;);

            }
            else {
                typename key_tracker_type::iterator itLRU = C_2i_key.begin();
                assert(itLRU != C_2i_key.end());
                typename key_to_value_type::iterator itLRUValue = C_2i.find(*itLRU);

                typename key_tracker_type::iterator itNew = C_2o_key.insert(C_2o_key.end(), *itLRU);
                // Create the key-value entry,
                // linked to the usage record.
                const V v_tmp = _fn(*itLRU, itLRUValue->second.first);
                C_2o.insert(make_pair(*itLRU, make_pair(v_tmp, itNew)));

                PRINTV(logfile << "REPLACE insert key to b2: " << *itLRU <<  "** b2 size: "<< C_2o.size() << endl;);

                PRINTV(logfile << "REPLACE evicting clean key without flushing back to DiskSim input trace " << *itLRU <<  endl;);

                PRINTV(logfile << "REPLACE Key clean bit status: " << bitset<10>(itLRUValue->second.first.getReq().flags) << endl;);

                C_2i.erase(itLRUValue);
                C_2i_key.remove(*itLRU);
                PRINTV(logfile << "REPLACE C_1i size: " << C_2i.size() <<endl;);

                PRINTV(logfile << "REPLACE insert key to C_2o: " << k <<  "** C_2o size: "<< C_2o.size() << endl;);

            }
        }
        else if( (D_1i.size() > 0) && ((D_1i.size() >= unsigned(P_D*(_capacity-P)))) ) {

            typename key_tracker_type::iterator itLRU = D_1i_key.begin();
            assert(itLRU != D_1i_key.end());
            typename key_to_value_type::iterator itLRUValue = D_1i.find(*itLRU);

            typename key_tracker_type::iterator itNew = D_1o_key.insert(D_1o_key.end(), *itLRU);
            // Create the key-value entry,
            // linked to the usage record.
            const V v_tmp = _fn(*itLRU, itLRUValue->second.first);
            D_1o.insert(make_pair(*itLRU, make_pair(v_tmp, itNew)));

            PRINTV(logfile << "REPLACE insert key to D_1o: " << *itLRU <<  "** D_1o size: "<< D_1o.size() << endl;);

            ///ziqi: DiskSim format Request_arrival_time Device_number Block_number Request_size Request_flags
            ///ziqi: Device_number is set to 1. About Request_flags, 0 is for write and 1 is for read
            PRINTV(DISKSIMINPUTSTREAM << setfill(' ')<<left<<fixed<<setw(25)<<v.getReq().issueTime<<left<<setw(8)<<"0"<<left<<fixed<<setw(12)<<itLRUValue->second.first.getReq().fsblkno<<left<<fixed<<setw(8)<<itLRUValue->second.first.getReq().reqSize<<"0"<<endl;);

            PRINTV(logfile << "REPLACE evicting dirty key " << *itLRU <<  endl;);
            totalPageWriteToStorage++;
            PRINTV(logfile << "REPLACE Key dirty bit status: " << bitset<10>(itLRUValue->second.first.getReq().flags) << endl;);

            ///afterCacheTrace
            PRINTV(AFTERCACHETRACE <<*itLRU<<"W"<<endl;);
            ///afterCacheTrace

            D_1i.erase(itLRUValue);
            D_1i_key.remove(*itLRU);
            PRINTV(logfile << "REPLACE D_1i size: " << D_1i.size() <<endl;);


        }
        else {
            typename key_tracker_type::iterator itLRU = D_2i_key.begin();
            assert(itLRU != D_2i_key.end());
            typename key_to_value_type::iterator itLRUValue = D_2i.find(*itLRU);

            typename key_tracker_type::iterator itNew = D_2o_key.insert(D_2o_key.end(), *itLRU);
            // Create the key-value entry,
            // linked to the usage record.
            const V v_tmp = _fn(*itLRU, itLRUValue->second.first);
            D_2o.insert(make_pair(*itLRU, make_pair(v_tmp, itNew)));

            PRINTV(logfile << "REPLACE insert key to D_2o: " << *itLRU <<  "** D_2o size: "<< D_2o.size() << endl;);

            ///ziqi: DiskSim format Request_arrival_time Device_number Block_number Request_size Request_flags
            ///ziqi: Device_number is set to 1. About Request_flags, 0 is for write and 1 is for read
            PRINTV(DISKSIMINPUTSTREAM << setfill(' ')<<left<<fixed<<setw(25)<<v.getReq().issueTime<<left<<setw(8)<<"0"<<left<<fixed<<setw(12)<<itLRUValue->second.first.getReq().fsblkno<<left<<fixed<<setw(8)<<itLRUValue->second.first.getReq().reqSize<<"0"<<endl;);

            PRINTV(logfile << "REPLACE evicting dirty key " << *itLRU <<  endl;);
            totalPageWriteToStorage++;
            PRINTV(logfile << "REPLACE Key dirty bit status: " << bitset<10>(itLRUValue->second.first.getReq().flags) << endl;);

            ///afterCacheTrace
            PRINTV(AFTERCACHETRACE <<*itLRU<<"W"<<endl;);
            ///afterCacheTrace

            D_2i.erase(itLRUValue);
            D_2i_key.remove(*itLRU);
            PRINTV(logfile << "REPLACE D_2i size: " << D_2i.size() <<endl;);
        }
    }

private:

// The function to be cached
    V(*_fn)(const K & , V);
// Maximum number of key-value pairs to be retained
    const size_t _capacity;

    unsigned levelMinusMinus;

    ///ziqi: Key access history
    key_tracker_type C_1i_key;
    ///ziqi: Key-to-value lookup
    key_to_value_type C_1i;
    ///ziqi: Key access history
    key_tracker_type C_2i_key;
    ///ziqi: Key-to-value lookup
    key_to_value_type C_2i;
    ///ziqi: Key access history
    key_tracker_type C_1o_key;
    ///ziqi: Key-to-value lookup
    key_to_value_type C_1o;
    ///ziqi: Key access history
    key_tracker_type C_2o_key;
    ///ziqi: Key-to-value lookup
    key_to_value_type C_2o;

    ///ziqi: Key access history
    key_tracker_type D_1i_key;
    ///ziqi: Key-to-value lookup
    key_to_value_type D_1i;
    ///ziqi: Key access history
    key_tracker_type D_2i_key;
    ///ziqi: Key-to-value lookup
    key_to_value_type D_2i;
    ///ziqi: Key access history
    key_tracker_type D_1o_key;
    ///ziqi: Key-to-value lookup
    key_to_value_type D_1o;
    ///ziqi: Key access history
    key_tracker_type D_2o_key;
    ///ziqi: Key-to-value lookup
    key_to_value_type D_2o;
};

#endif //end lru_stl
