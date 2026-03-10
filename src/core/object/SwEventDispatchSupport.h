#pragma once

class SwEvent;
class SwObject;

bool swIsObjectLive(const SwObject* object);
bool swDispatchEventToObject(SwObject* receiver, SwEvent* event);
bool swDispatchEventFilter(SwObject* filter, SwObject* watched, SwEvent* event);
bool swDispatchInstalledEventFilters(SwObject* watched, SwEvent* event);
bool swForwardPostedEventToReceiverThread(SwObject* receiver, SwEvent* event, int priority);
