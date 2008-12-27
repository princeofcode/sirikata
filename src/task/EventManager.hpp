/*     Iridium Kernel -- Task scheduling system
 *  EventManager.hpp
 *
 *  Copyright (c) 2008, Patrick Reiter Horn
 *  All rights reserved.
 * 
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are
 *  met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name of Iridium nor the names of its contributors may
 *    be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER
 * OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef IRIDIUM_EventManager_HPP__
#define IRIDIUM_EventManager_HPP__

#include <boost/function.hpp>
#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/weak_ptr.hpp>
#include <string>
#include <algorithm>
#include <map>
#include <list>
#include <vector>

#include "HashMap.hpp"
#include "Subscription.hpp"
#include "Event.hpp"
#include "Time.hpp"

/** @namespace Iridium::Task 
 * Iridium::Task contains the task-oriented functions for communication
 * across the program, as well as a scheduler to manage space CPU cycles
 * between graphics frames.
 */

namespace Iridium {

/**
 * EventManager.hpp -- EventManager class definition, as well as the
 * EventResponse and EventOrder enumerations.
 */
namespace Task {

// TODO: Add events with timeouts.

// TODO: If two people register two events with the same removeId,
// it may be better to store two copies of the event and have unsubscribe
// only unsubscribe one of those two (use a multi_map for mRemoveById.



/**
 * Defines the set of return values for an EventListener. An acceptable
 * value includes the bitwise or of any values in the enum.
 */
class EventResponse {
	enum {
		NOP,
		DELETE_LISTENER=1,
		CANCEL_EVENT=2,
		DELETE_LISTENER_AND_CANCEL_EVENT = DELETE_LISTENER | CANCEL_EVENT,
	} mResp;

	template <class Ev> friend class EventManager;

public:
	static EventResponse nop() {
		EventResponse retval;
		retval.mResp=NOP;
		return retval;
	}
	static EventResponse del() {
		EventResponse retval;
		retval.mResp=DELETE_LISTENER;
		return retval;
	}
	static EventResponse cancel() {
		EventResponse retval;
		retval.mResp=CANCEL_EVENT;
		return retval;
	}
	static EventResponse cancelAndDel() {
		EventResponse retval;
		retval.mResp=DELETE_LISTENER_AND_CANCEL_EVENT;
		return retval;
	}
};

/**
 * Defines constants to allow a strict ordering of event processing.
 * At the moment, since there is not a good use case for this,
 * there are only three legal orderings specified.
 */
enum EventOrder {
	EARLY,
	MIDDLE,
	LATE,
	NUM_EVENTORDER
};

/// Exception thrown if an invalid EventOrder is passed.
class EventOrderException : std::exception {};

/** Some EventManagers may require a different base class which
 * inherits from Event but have additional properties. */
template <class EventBase>
class EventManager {

	/* TYPEDEFS */
public:
	/// A shared_ptr to an Event.
	typedef boost::shared_ptr<EventBase> EventPtr;

	/**
	 * A boost::function1 taking an Event and returning a value indicating
	 * Whether to cancel the event, remove the event responder, or some
	 * other values.
	 *
	 * @see EventResponse
	 */
	typedef boost::function1<EventResponse, EventPtr> EventListener;

private:
	/// if the listener does not corresond to an id, use SubscriptionId::null().
	typedef std::pair<EventListener, SubscriptionId> ListenerSubscriptionInfo;
	typedef std::list<ListenerSubscriptionInfo> ListenerList;
	class PartiallyOrderedListenerList {
		ListenerList ll [NUM_EVENTORDER];
	public:
			ListenerList &operator [] (size_t i) {
				return ll[i];
			}
		};

	class EventSubscriptionInfo {
		ListenerList &mList;
		typename ListenerList::iterator mIter;
		friend class EventManager<EventBase>;
	public:

		EventSubscriptionInfo(ListenerList &list,
						typename ListenerList::iterator &iter)
			: mList(list), mIter(iter) {
		}
	};
	
	typedef HashMap<IdPair::Secondary, PartiallyOrderedListenerList,
				IdPair::Secondary::Hasher> SecondaryListenerMap;
	typedef std::pair<PartiallyOrderedListenerList, SecondaryListenerMap> PrimaryListenerInfo;
	typedef std::map<IdPair::Primary, PrimaryListenerInfo> PrimaryListenerMap;
	typedef HashMap<SubscriptionId, EventSubscriptionInfo,
				SubscriptionId::Hasher> RemoveMap;
	typedef std::vector<EventPtr> EventList;
	
	/* MEMBERS */

	PrimaryListenerMap mListeners;
	EventList mUnprocessed;
	RemoveMap mRemoveById; ///< Used for unsubscribe: always keep in sync.

	bool mProcessing; ///< we are not allowed to immediately remove listeners.
	bool mClearCurrentList;
	ListenerList *mProcessingList; ///< if non-NULL, do not allow removes from this list.
	
	std::list<SubscriptionId> mUnsubscribeList;

	/* PRIVATE FUNCTIONS */

	PrimaryListenerInfo &insertPriId(const IdPair::Primary &pri);
	PartiallyOrderedListenerList &insertSecId(SecondaryListenerMap &map,
				const IdPair::Secondary &sec);

	/// Removee if the passed element has no items left.
	/// Call after anything that could remove items from the map.
	bool cleanUp(typename PrimaryListenerMap::iterator &iter);
	bool cleanUp(SecondaryListenerMap &slm,
				typename SecondaryListenerMap::iterator &slm_iter);


	int clearListenerList(ListenerList &list);

	bool fireAll(EventPtr ev, ListenerList &lili, AbsTime forceCompletionBy);
public:
	/* PUBLIC FUNCTIONS */
	/// FIXME: This is for testing purposes only--do not make public.
	void temporary_processEventQueue(AbsTime forceCompletionBy);

	/**
	 * Subscribes to a specific event. The listener function will receieve
	 * only events whose type matches eventId.mPriId, and whose secondary
	 * information matches eventId.mSecId.
	 *
	 * Using this function, the listener may be unsubscribed only if it
	 * returns DELETE_LISTENER after being called for an event.
	 *
	 * @param eventId    the specific event to subscribe to
	 * @param listener   a (usually bound) boost::function taking an EventPtr
     * @param when       Guarantees a specific ordering. Defaults to MIDDLE.
	 *
	 * @see EventResponse
	 * @see EventListener
	 */
	void subscribe(const IdPair &eventId,
				const EventListener &listener,
				EventOrder when=MIDDLE);
	/**
	 * Subscribes to a given event type. The listener function will receieve ALL
	 * Events for the primaryId, no matter what SecondaryId it has.
	 *
	 * Using this function, the listener may be unsubscribed only if it
	 * returns DELETE_LISTENER after being called for an event.
	 *
	 * @param primaryId  the event type to subscribe to
	 * @param listener   a (usually bound) boost::function taking an EventPtr
     * @param when       Guarantees a specific ordering. Defaults to MIDDLE.
	 *
	 * @see EventResponse
	 * @see EventListener
	 */
	void subscribe(const IdPair::Primary & primaryId,
				const EventListener & listener,
				EventOrder when=MIDDLE);
	
	/**
	 * Subscribes to a specific event. The listener function will receieve
	 * only events whose type matches eventId.mPriId, and whose secondary
	 * information matches eventId.mSecId.
	 *
	 * The removeId specifies an identifier, generated by the GEN_ID or CLASS_ID
	 * macros, which may later be passed into unsubscribe.  The handler function
	 * may also unsubscribe the event by returning DELETE_LISTENER. Note also that
	 * if two subscriptions are created with the same removeId, the original is
	 * unsubscribed and superceded by this listener.
	 *
	 * @param eventId    the specific event to subscribe to
	 * @param listener   a (usually bound) boost::function taking an EventPtr
	 * @param removeId   a SubscriptionId that can be passed to unsubscribe
     * @param when       Guarantees a specific ordering. Defaults to MIDDLE.
	 *
	 * @see SubscriptionId
	 * @see EventResponse
	 * @see EventListener
	 */
	void subscribe(const IdPair & eventId,
				const EventListener & listener,
				const SubscriptionId & removeId,
				EventOrder when=MIDDLE);
	/**
	 * Subscribes to a given event type. The listener function will receieve ALL
	 * Events for the primaryId, no matter what SecondaryId it has.
	 *
	 * The removeId specifies an identifier, generated by the GEN_ID or CLASS_ID
	 * macros, which may later be passed into unsubscribe.  The handler function
	 * may also unsubscribe the event by returning DELETE_LISTENER. Note also that
	 * if two subscriptions are created with the same removeId, the original is
	 * unsubscribed and superceded by this listener.
	 *
	 * @param primaryId  the event type to subscribe to
	 * @param listener   a (usually bound) boost::function taking an EventPtr
	 * @param removeId   a SubscriptionId that can be passed to unsubscribe
     * @param when       Guarantees a specific ordering. Defaults to MIDDLE.
	 *
	 * @see SubscriptionId
	 * @see EventResponse
	 * @see EventListener
	 */
	void subscribe(const IdPair::Primary & primaryId,
				const EventListener & listener,
				const SubscriptionId & removeId,
				EventOrder when=MIDDLE);
	
	/**
	 * Unsubscribes from the event matching removeId. The removeId should be
	 * created using the same CLASS_ID or GEN_ID macros that were used when
	 * creating the subscription.
	 *
	 * @param removeId  the exact SubscriptionID to search for.
	 */
	void unsubscribe(const SubscriptionId &removeId);

	/**
	 * Removes all listeners which are specifically waiting for the given IdPair.
	 * 
	 * @param whichId  an IdPair equal to the one subscribed.
	 * @returns        the number of EventListeners which matched whichId
	 */
	int removeAllByInterest(const IdPair &whichId);
	/**
	 * Removes all listeners which are waiting for any event of this event type.
	 * Since the event system supports both generic (any event of a type) and
	 * specific (specific occurence of an event) listeners, two boolean flags 
	 * pecify which types are to be removed.
	 * 
	 * [NOTE: Is a function like this necessary? useful? encourages bad style?]
	 * 
	 * @param whichId   a PrimaryId equal to the one subscribed.
	 * @param specific  true if specific subscribers should be removed (default true)
	 * @param generic   true if generic subscribers should be removed (default true)
	 * @returns         the number of EventListeners which matched whichId
	 */
	int removeAllByInterest(const IdPair::Primary &whichId,
				bool generic=true,
				bool specific=true);

	/**
	 * Puts the passed event into the mUnprocessed event queue, which will be
	 * fired at the end of the frame corresponding to its IdPair. See the Event
	 * class for more information.
	 * 
	 * @param ev  A shared_ptr to an Event to be stored in the queue.
	 * @see   Event
	 */
	void fire(EventPtr ev);

};

/**
 * Generic Event Manager -- the most common type that accepts any
 * subclass of Event.
 */
typedef class EventManager<Event> GenEventManager;

}
}

#endif
