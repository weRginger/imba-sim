//
// C++ Interface: CLOCK with Adaptive Replacement
//
// Description:
//
//
// Author: ARH,,, <arh@aspire-one>, (C) 2011
//
// Copyright: See COPYING file that comes with this distribution
//

#ifndef CAR_H
#define CAR_H

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

// Class providing fixed-size (by number of records)
// LRU-replacement cache of a function with signature
// V f(K)
template <typename K, typename V>
class CAR : public TestCache<K, V>
{
public:
    // Key access history, most recent at back
    typedef list<K> key_tracker_type;
    // Key to value and key history iterator
    typedef map< K, V> key_to_value_type;
    // store the clock hand, the value is the key of the key_to_value_type
    // when need to evict, clock scans starting from the item with the key stored in clockHand
    uint32_t clockHand_t1 = 0;
    uint32_t clockHand_t2 = 0;
    // for first item in trace, it is used to initiate the clock hand
    bool initClockHand_t1 = true;
    bool initClockHand_t2 = true;

    CAR(
        V(*f)(const K & , V),
        size_t c,
        unsigned levelMinus
    ) : _fn(f) , _capacity(c), levelMinusMinus(levelMinus)  {
    }


    uint32_t access(const K &k  , V &value, uint32_t status) {
        PRINTV(logfile << endl;);
        // ziqi: p denotes the length of t1 and (_capacity - p) denotes the lenght of t2
        static int p=0;

        assert((t1.size() + t2.size()) <= _capacity);
        assert((t1.size() + b1.size()) <= _capacity);
        assert((t2.size() + b2.size()) <= 2*_capacity);
        assert((t1.size() + t2.size() + b1.size() + b2.size()) <= 2*_capacity);
        assert(_capacity != 0);
        PRINTV(logfile << "Access key: " << k << endl;);

        // Attempt to find existing record
        const typename key_to_value_type::iterator it_t1 = t1.find(k);
        const typename key_to_value_type::iterator it_t2 = t2.find(k);
        const typename key_to_value_type::iterator it_b1 = b1.find(k);
        const typename key_to_value_type::iterator it_b2 = b2.find(k);

        ///ziqi: CAR Case I: x hit in t1 or t2, set the page to hot
        if((it_t1 != t1.end()) || (it_t2 != t2.end())) {
            assert(!((it_t1 != t1.end()) && (it_t2 != t2.end())));
            if((it_t1 != t1.end()) && (it_t2 == t2.end())) {
                PRINTV(logfile << "Case I Hit on t1 " << k << endl;);

                PRINTV(logfile << "before flags: " << bitset<13>(it_t1->second.getFlags()) << endl;);
                //if the current page is cold, change it to hot
                if(it_t1->second.getFlags() & COLD) {
                    it_t1->second.updateFlags(it_t1->second.getFlags() ^ COLD);
                }
                PRINTV(logfile << "after flags: " << bitset<13>(it_t1->second.getFlags()) << endl;);

                PRINTV(logfile << "Case I: " << k << "** t1 size: "<< t1.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
            }

            if((it_t2 != t2.end()) && (it_t1 == t1.end())) {
                PRINTV(logfile << "Case I Hit on t2 " << k << endl;);

                PRINTV(logfile << "before flags: " << bitset<13>(it_t2->second.getFlags()) << endl;);
                //if the current page is cold, change it to hot
                if(it_t2->second.getFlags() & COLD) {
                    it_t2->second.updateFlags(it_t2->second.getFlags() ^ COLD);
                }
                PRINTV(logfile << "after flags: " << bitset<13>(it_t2->second.getFlags()) << endl;);

                PRINTV(logfile << "Case I: " << k << "** t1 size: "<< t1.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
            }
            return (status | PAGEHIT | BLKHIT);
        }
        // CAR: Cache miss
        else {
            // cache is full
            if( (t1.size() + t2.size()) == _capacity) {
                const V v = _fn(k, value);
                REPLACE(k, v, p);

                //evict LRU page from b1
                if( (it_b1 == b1.end() && it_b2 == b2.end()) && (t1.size() + b1.size()) == _capacity)
                {
                    typename key_tracker_type::iterator itLRU = b1_key.begin();
                    assert(itLRU != b1_key.end());
                    typename key_to_value_type::iterator it = b1.find(*itLRU);
                    b1.erase(it);
                    b1_key.remove(*itLRU);
                    PRINTV(logfile << "Evicting LRU page from b1 " << *itLRU <<  endl;);
                }
                //evict LRU page from b2
                else if( (t1.size() + t2.size() + b1.size() + b2.size()) == 2*_capacity && (it_b1 == b1.end() && it_b2 == b2.end()) ) {
                    typename key_tracker_type::iterator itLRU = b2_key.begin();
                    assert(itLRU != b2_key.end());
                    typename key_to_value_type::iterator it = b2.find(*itLRU);
                    b2.erase(it);
                    b2_key.remove(*itLRU);
                    PRINTV(logfile << "Evicting LRU page from b2 " << *itLRU <<  endl;);
                }
            }

            // b1 and b2 miss also, insert x at the tail of t1
            if( it_b1 == b1.end() && it_b2 == b2.end() ) {
		///insert page
		//for the very first item in the trace, use it as the clockHand
		if(initClockHand_t1) {
		    initClockHand_t1 = false;

		    t1_key.insert(t1_key.end(), k);
		    //set the page to be cold before insertion
		    value.updateFlags(value.getFlags() | COLD);
		    const V v = _fn(k, value);
		    t1.insert(make_pair(k, v));
		    PRINTV(logfile << "First time insert page to clock t1: " << k << endl;);
		    PRINTV(logfile << "Cache utilization: " << "** t1 size: "<< t1.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);

		    //init clockHand to first cached item
		    typename key_tracker_type::iterator it_list_clock = t1_key.begin();
		    clockHand_t1 = *it_list_clock;
		    PRINTV(logfile <<__LINE__ << "clockHand_t1 " << clockHand_t1 << endl;);
		}
		//if not the very first item, clockHand is initiated already
		else {
		  //find the clockHand from clock t1
		  //and insert the new item just in front of the clockHand in clock t1
		  typename key_tracker_type::iterator it_list_clock = t1_key.begin();
		  for(; it_list_clock != t1_key.end(); it_list_clock++) {
		      PRINTV(logfile << "clockHand_t1 " << clockHand_t1 << " *it_list_clock " << *it_list_clock << endl;);
		      if(*it_list_clock == clockHand_t1) {
			  PRINTV(logfile << "found clockHand_t1 " << clockHand_t1 << endl;);
			  break;
		      }
		  }
		  assert(it_list_clock != _key_tracker.end());
		  assert(!_key_tracker.empty());
		  t1_key.insert(it_list_clock, k);
		  //set the page to be cold before insertion
		  value.updateFlags(value.getFlags() | COLD);
		  const V v = _fn(k, value);
		  t1.insert(make_pair(k, v));
		  PRINTV(logfile << "Insert page to clock t1: " << k << endl;);
		  PRINTV(logfile << "Cache utilization: " << "** t1 size: "<< t1.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
		}
            }
            // b1 hit
            else if ( it_b1 != b1.end() ) {
	      assert(it_b2 == b2.end());
	      
	      // adaptation
	      int delta;
	      if(b1.size() >= b2.size()) {
                delta = 1;
	      }
	      else {
                delta = int(b2.size()/b1.size());
	      }
	      PRINTV(logfile << "ADAPTATION: p increases from " << p << " to ";);
	      if((p+delta) > int(_capacity)) {
		  p = _capacity;
	      }
	      else {
		  p = p+delta;
	      }
	      PRINTV(logfile << p << endl;);
	      
	      // delete x from b1
	      b1.erase(it_b1);
	      b1_key.remove(k);
	      
	      // insert x to tail of clock t2
	      
	      // for the very first item in the trace, use it as the clockHand
	      if(initClockHand_t2) {
		  initClockHand_t2 = false;

		  t2_key.insert(t2_key.end(), k);
		  //set the page to be cold before insertion
		  value.updateFlags(value.getFlags() | COLD);
		  const V v = _fn(k, value);
		  t2.insert(make_pair(k, v));
		  PRINTV(logfile << "Insert page to clock: " << k << endl;);
		  PRINTV(logfile << "Cache utilization: " << "** t1 size: "<< t1.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);

		  //init clockHand to first cached item
		  typename key_tracker_type::iterator it_list_clock = t2_key.begin();
		  clockHand_t2 = *it_list_clock;
		  PRINTV(logfile <<__LINE__ << "clockHand_t2 " << clockHand_t2 << endl;);
	      }
	      //if not the very first item, clockHand is initiated already
	      else {
		  //find the clockHand from clock list
		  //and insert the new item just in front of the clockHand in clock list
		  typename key_tracker_type::iterator it_list_clock = t2_key.begin();
		  for(; it_list_clock != t2_key.end(); it_list_clock++) {
		      PRINTV(logfile << "clockHand_t2 " << clockHand_t2 << " *it_list_clock " << *it_list_clock << endl;);
		      if(*it_list_clock == clockHand_t2) {
			  PRINTV(logfile << "found clockHand_t2 " << clockHand_t2 << endl;);
			  break;
		      }
		  }
		  assert(it_list_clock != t2_key.end());
		  assert(!t2_key.empty());
		  t2_key.insert(it_list_clock, k);
		  //set the page to be cold before insertion
		  value.updateFlags(value.getFlags() | COLD);
		  const V v = _fn(k, value);
		  t2.insert(make_pair(k, v));
		  PRINTV(logfile << "Insert page to clock t2: " << k << endl;);
		  PRINTV(logfile << "Cache utilization: " << "** t1 size: "<< t1.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
	      }
            }
            // b2 hit
            else {
	      assert(it_b2 != b2.end());
	      assert(it_b1 == b1.end());
	      
	      // adaptation
	      int delta;
	      if(b2.size() >= b1.size()) {
                delta = 1;
	      }
	      else {
                delta = int(b1.size()/b2.size());
	      }
	      PRINTV(logfile << "ADAPTATION: p decreases from " << p << " to ";);
	      if((p-delta) > 0) {
		  p = p-delta;
	      }
	      else {
		  p = 0;
	      }
	      PRINTV(logfile << p << endl;);
	      
	      // delete x from b2
	      b2.erase(it_b2);
	      b2_key.remove(k);
	      
	      // insert x to tail of clock t2
	      
	      // for the very first item in the trace, use it as the clockHand
	      if(initClockHand_t2) {
		  initClockHand_t2 = false;

		  t2_key.insert(t2_key.end(), k);
		  //set the page to be cold before insertion
		  value.updateFlags(value.getFlags() | COLD);
		  const V v = _fn(k, value);
		  t2.insert(make_pair(k, v));
		  PRINTV(logfile << "Insert page to clock: " << k << endl;);
		  PRINTV(logfile << "Cache utilization: " << "** t1 size: "<< t1.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);

		  //init clockHand to first cached item
		  typename key_tracker_type::iterator it_list_clock = t2_key.begin();
		  clockHand_t2 = *it_list_clock;
		  PRINTV(logfile <<__LINE__ << "clockHand_t2 " << clockHand_t2 << endl;);
	      }
	      //if not the very first item, clockHand is initiated already
	      else {
		  //find the clockHand from clock list
		  //and insert the new item just in front of the clockHand in clock list
		  typename key_tracker_type::iterator it_list_clock = t2_key.begin();
		  for(; it_list_clock != t2_key.end(); it_list_clock++) {
		      PRINTV(logfile << "clockHand_t2 " << clockHand_t2 << " *it_list_clock " << *it_list_clock << endl;);
		      if(*it_list_clock == clockHand_t2) {
			  PRINTV(logfile << "found clockHand_t2 " << clockHand_t2 << endl;);
			  break;
		      }
		  }
		  assert(it_list_clock != t2_key.end());
		  assert(!t2_key.empty());
		  t2_key.insert(it_list_clock, k);
		  //set the page to be cold before insertion
		  value.updateFlags(value.getFlags() | COLD);
		  const V v = _fn(k, value);
		  t2.insert(make_pair(k, v));
		  PRINTV(logfile << "Insert page to clock t2: " << k << endl;);
		  PRINTV(logfile << "Cache utilization: " << "** t1 size: "<< t1.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
	      }
            }
        }

	///ziqi: CAR subroutine
	void REPLACE(const K &k, const V &v, int p) {
	  // **********************************************
	}





        // *********************************************************************
        ///ziqi: CAR Case II: x hit in b1, then enlarge t1 and move x from b1 to t2
        else if(it_b1 != b1.end()) {
            ///ziqi: delta denotes the step in each ADAPTATION
            int delta;

            PRINTV(logfile << "Case II Hit on b1: " << k << endl;);

            ///Disk read after a cache miss
            ///ziqi: DiskSim format Request_arrival_time Device_number Block_number Request_size Request_flags
            ///ziqi: Device_number is set to 1. About Request_flags, 0 is for write and 1 is for read
            PRINTV(DISKSIMINPUTSTREAM << setfill(' ')<<left<<fixed<<setw(25)<<value.getReq().issueTime<<left<<setw(8)<<"0"<<left<<fixed<<setw(12)<<k<<left<<fixed<<setw(8)<<"1"<<"1"<<endl;);

            if(b1.size() >= b2.size())
                delta = 1;
            else
                delta = int(b2.size()/b1.size());

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

            PRINTV(logfile << "Case II evicting b1 " << k <<  endl;);

            // Record k as most-recently-used key
            typename key_tracker_type::iterator itNew
                = t2_key.insert(t2_key.end(), k);
            // Create the key-value entry,
            // linked to the usage record.
            t2.insert(make_pair(k, make_pair(v, itNew)));
            PRINTV(logfile << "Case II insert key to t2: " << k << "** t1 size: "<< t1.size()<< ", t2 size: "<< t2.size() <<endl;);
            return (status | PAGEMISS);
        }
        ///ziqi: ARC Case III: x hit in b2, then enlarge t2 and move x from b2 to t2
        else if(it_b2 != b2.end()) {
            ///ziqi: delta denotes the step in each ADAPTATION
            int delta;

            PRINTV(logfile << "Case III Hit on b2: " << k << endl;);

            ///Disk read after a cache miss
            ///ziqi: DiskSim format Request_arrival_time Device_number Block_number Request_size Request_flags
            ///ziqi: Device_number is set to 1. About Request_flags, 0 is for write and 1 is for read
            PRINTV(DISKSIMINPUTSTREAM << setfill(' ')<<left<<fixed<<setw(25)<<value.getReq().issueTime<<left<<setw(8)<<"0"<<left<<fixed<<setw(12)<<k<<left<<fixed<<setw(8)<<"1"<<"1"<<endl;);

            if(b2.size() >= b1.size())
                delta = 1;
            else
                delta = int(b1.size()/b2.size());

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

            PRINTV(logfile << "Case III evicting b2 " << k <<  endl;);

            // Record k as most-recently-used key
            typename key_tracker_type::iterator itNew
                = t2_key.insert(t2_key.end(), k);
            // Create the key-value entry,
            // linked to the usage record.
            t2.insert(make_pair(k, make_pair(v, itNew)));
            PRINTV(logfile << "Case III insert key to t2: " << k << "** t1 size: "<< t1.size()<< ", t2 size: "<< t2.size() <<endl;);
            return (status | PAGEMISS);
        }
        ///ziqi: ARC Case IV: x is cache miss
        else {
            PRINTV(logfile << "Case IV miss on key: " << k << endl;);
            PRINTV(logfile << "Case IV status: ** t1 size: "<< t1.size()<< ", t2 size: "<< t2.size() <<", b1 size: "<< b1.size() <<", b2 size: "<< b2.size() <<endl;);
            ///ziqi: Case A
            if((t1.size() + b1.size()) == _capacity) {
                if(t1.size() < _capacity) {
                    typename key_tracker_type::iterator itLRU = b1_key.begin();
                    assert(itLRU != b1_key.end());
                    typename key_to_value_type::iterator it = b1.find(*itLRU);
                    b1.erase(it);
                    b1_key.remove(*itLRU);
                    ///PRINTV(logfile << "Case IV evicting b1 " << *itLRU <<  endl;);

                    const V v = _fn(k, value);

                    REPLACE(k, v, p);
                }
                else {
                    typename key_tracker_type::iterator itLRU = t1_key.begin();
                    assert(itLRU != t1_key.end());
                    typename key_to_value_type::iterator it = t1.find(*itLRU);
                    t1.erase(it);
                    t1_key.remove(*itLRU);
                }
            }
            ///ziqi: Case B
            else if((t1.size() + b1.size()) < _capacity) {
                if((t1.size() + t2.size() + b1.size() + b2.size()) >= _capacity) {
                    if((t1.size() + t2.size() + b1.size() + b2.size()) == 2 * _capacity) {
                        typename key_tracker_type::iterator itLRU = b2_key.begin();
                        assert(itLRU != b2_key.end());
                        typename key_to_value_type::iterator it = b2.find(*itLRU);
                        b2.erase(it);
                        b2_key.remove(*itLRU);
                    }

                    const V v = _fn(k, value);

                    REPLACE(k, v, p);
                }
            }

            const V v = _fn(k, value);
            // Record k as most-recently-used key
            typename key_tracker_type::iterator itNew = t1_key.insert(t1_key.end(), k);
            // Create the key-value entry,
            // linked to the usage record.
            t1.insert(make_pair(k, make_pair(v, itNew)));
            ///PRINTV(logfile << "Case IV insert key to t1: " << k << "** t1 size: "<< t1.size()<< ", t2 size: "<< t2.size() <<endl;);
            return (status | PAGEMISS);
        }

        return 0;

    } //end operator access

    ///ziqi: ARC subroutine
    void REPLACE(const K &k, const V &v, int p) {
        typename key_to_value_type::iterator it = b2.find(k);

        if((t1.size() > 0) && ((t1.size() > unsigned(p)) || ((it != b2.end()) && (t1.size() == unsigned(p))))) {
            typename key_tracker_type::iterator itLRU = t1_key.begin();
            assert(itLRU != t1_key.end());
            typename key_to_value_type::iterator itLRUValue = t1.find(*itLRU);

            // Record k as most-recently-used key
            typename key_tracker_type::iterator itNew = b1_key.insert(b1_key.end(), *itLRU);
            // Create the key-value entry,
            // linked to the usage record.
            const V v_tmp = _fn(*itLRU, itLRUValue->second.first);
            b1.insert(make_pair(*itLRU, make_pair(v_tmp, itNew)));
            PRINTV(logfile << "REPLACE insert key to b1: " << *itLRU <<  "** b1 size: "<< b1.size() << endl;);
            ///PRINTV(logfile << "REPLACE Key clean bit status: " << bitset<10>(itLRUValue->second.first.getReq().flags) << endl;);
            t1.erase(itLRUValue);
            t1_key.remove(*itLRU);
            ///PRINTV(logfile << "REPLACE t1 size: " << t1.size() <<endl;);
        }
        else {
            typename key_tracker_type::iterator itLRU = t2_key.begin();
            assert(itLRU != t2_key.end());
            typename key_to_value_type::iterator itLRUValue = t2.find(*itLRU);

            // Record k as most-recently-used key
            typename key_tracker_type::iterator itNew = b2_key.insert(b2_key.end(), *itLRU);
            // Create the key-value entry,
            // linked to the usage record.
            const V v_tmp = _fn(*itLRU, itLRUValue->second.first);
            b2.insert(make_pair(*itLRU, make_pair(v_tmp, itNew)));
            PRINTV(logfile << "REPLACE insert key to b2: " << *itLRU <<  "** b2 size: "<< b2.size() << endl;);

            ///ziqi: DiskSim format Request_arrival_time Device_number Block_number Request_size Request_flags
            ///ziqi: Device_number is set to 1. About Request_flags, 0 is for write and 1 is for read
            PRINTV(DISKSIMINPUTSTREAM << setfill(' ')<<left<<fixed<<setw(25)<<v.getReq().issueTime<<left<<setw(8)<<"0"<<left<<fixed<<setw(12)<<*itLRU<<left<<fixed<<setw(8)<<"1"<<"0"<<endl;);
            // Erase both elements to completely purge record
            ///PRINTV(logfile << "REPLACE evicting dirty key " << *itLRU <<  endl;);
            totalPageWriteToStorage++;
            ///PRINTV(logfile << "REPLACE Key dirty bit status: " << bitset<10>(itLRUValue->second.first.getReq().flags) << endl;);
            t2.erase(itLRUValue);
            t2_key.remove(*itLRU);

            ///PRINTV(logfile << "REPLACE t2 size: " << t2.size() <<endl;);
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
};

#endif //end lru_stl
