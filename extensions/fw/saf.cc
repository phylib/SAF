#include "saf.h"

using namespace nfd;
using namespace nfd::fw;

NFD_LOG_INIT("SAF");

const Name SAF::STRATEGY_NAME("ndn:/localhost/nfd/strategy/saf");

SAF::SAF(Forwarder &forwarder, const Name &name) : Strategy(forwarder, name)
{
  const FaceTable& ft = getFaceTable();
  engine = boost::shared_ptr<SAFEngine>(new SAFEngine(ft, (int) ParameterConfiguration::getInstance ()->getParameter ("PREFIX_COMPONENT")));

  this->afterAddFace.connect([this] (shared_ptr<Face> face)
  {
    engine->addFace (face);
  });

  this->beforeRemoveFace.connect([this] (shared_ptr<Face> face)
  {
    engine->removeFace (face);
  });
}

SAF::~SAF()
{
}

void SAF::afterReceiveInterest(const Face& inFace, const Interest& interest ,shared_ptr<fib::Entry> fibEntry, shared_ptr<pit::Entry> pitEntry)
{
  /* Attention!!! interest != pitEntry->interest*/ // necessary to emulate NACKs in ndnSIM2.0
  /* interst could be /NACK/suffix, while pitEntry->getInterest is /suffix */

  //fprintf(stderr, "In f[%d]= %s\n", inFace.getId (),interest.getName ().toUri ().c_str ());

  //find + exclude inface(s) and already tried outface(s)
  NFD_LOG_DEBUG(interest << " from=" << inFace.getId());
  std::vector<int> originInFaces = getAllInFaces(pitEntry);
  std::vector<int> alreadyTriedFaces; // keep them empty for now and check if nack or retransmission?

  if (pitEntry->hasUnexpiredOutRecords() && ParameterConfiguration::getInstance ()->getParameter ("RTX_DETECTION") > 0)
  {
    if(isRtx(inFace, interest))
    {
      alreadyTriedFaces = getAllOutFaces(pitEntry); //definitely a rtx
    }
    else
    {
      addToKnownInFaces(inFace, interest); // maybe other client/node requests same content?
      return;
    } 
  }

  addToKnownInFaces(inFace, interest);

  const Interest int_to_forward = pitEntry->getInterest();
  int nextHop = engine->determineNextHop(int_to_forward, alreadyTriedFaces, fibEntry);
  while(nextHop != DROP_FACE_ID && (std::find(originInFaces.begin (),originInFaces.end (), nextHop) == originInFaces.end ()))
  {
    bool success = engine->tryForwardInterest (int_to_forward, getFaceTable ().get (nextHop));

    /*DISABLING LIMITS FOR NOW*/
    success = true; // as not used in the SAF paper.

    if(success)
    {
      //fprintf(stderr, "Transmitting %s on face[%d]\n", int_to_forward.getName().toUri().c_str(), nextHop);
      sendInterest(pitEntry, getFaceTable ().get (nextHop));
      return;
    }

    engine->logNack((*getFaceTable ().get(nextHop)), pitEntry->getInterest()); // this should be valid we never send the interest as limits forbids it
    alreadyTriedFaces.push_back (nextHop);
    nextHop = engine->determineNextHop(int_to_forward, alreadyTriedFaces, fibEntry);
  }

  for(unsigned int i = 0; i < alreadyTriedFaces.size (); i++)
  {
    engine->logRejectedInterest (pitEntry, alreadyTriedFaces.at (i)); // log not satisfied on all tried faces
  }
  engine->logRejectedInterest(pitEntry, nextHop);
  clearKnownFaces(int_to_forward);
  NFD_LOG_DEBUG(interest << " Send NACK");
  sendNACKToAllInFaces(pitEntry);
  rejectPendingInterest(pitEntry);
}

void SAF::beforeSatisfyInterest(shared_ptr<pit::Entry> pitEntry,const Face& inFace, const Data& data)
{  

  const pit::OutRecordCollection& records = pitEntry->getOutRecords();
  for (auto it = records.begin(); it != records.end(); it++) {
    if((*it).getFace()->getId() != inFace.getId ())
      engine->logNack ((*getFaceTable ().get((*it).getFace()->getId())), pitEntry->getInterest()); //its not a nack but this log has the same effect
  }

  engine->logSatisfiedInterest (pitEntry, inFace, data);
  clearKnownFaces(pitEntry->getInterest());
  Strategy::beforeSatisfyInterest (pitEntry,inFace, data);
}

void SAF::beforeExpirePendingInterest(shared_ptr< pit::Entry > pitEntry)
{
  engine->logExpiredInterest(pitEntry);
  clearKnownFaces(pitEntry->getInterest());
  Strategy::beforeExpirePendingInterest (pitEntry);
}



void
SAF::afterReceiveNack(const Face& inFace, const lp::Nack& nack,
                                     shared_ptr<fib::Entry> fibEntry,
                                     shared_ptr<pit::Entry> pitEntry)
{
  // Adapted code from afterReceiveInterest (Part for Interests-Only deleted)
  NFD_LOG_DEBUG("NACK from=" << inFace.getId());

  std::vector<int> originInFaces = getAllInFaces(pitEntry);
  std::vector<int> alreadyTriedFaces; // keep them empty for now and check if nack or retransmission?

  alreadyTriedFaces = getAllOutFaces(pitEntry);

  const Interest int_to_forward = pitEntry->getInterest();
  int nextHop = engine->determineNextHop(int_to_forward, alreadyTriedFaces, fibEntry);
  while(nextHop != DROP_FACE_ID && (std::find(originInFaces.begin (),originInFaces.end (), nextHop) == originInFaces.end ()))
  {
    bool success = engine->tryForwardInterest (int_to_forward, getFaceTable ().get (nextHop));

    /*DISABLING LIMITS FOR NOW*/
    success = true; // as not used in the SAF paper.

    if(success)
    {
      //fprintf(stderr, "Transmitting %s on face[%d]\n", int_to_forward.getName().toUri().c_str(), nextHop);
      sendInterest(pitEntry, getFaceTable ().get (nextHop));
      return;
    }

    engine->logNack((*getFaceTable ().get(nextHop)), pitEntry->getInterest()); // this should be valid we never send the interest as limits forbids it
    alreadyTriedFaces.push_back (nextHop);
    nextHop = engine->determineNextHop(int_to_forward, alreadyTriedFaces, fibEntry);
  }

  for(unsigned int i = 0; i < alreadyTriedFaces.size (); i++)
  {
    engine->logRejectedInterest (pitEntry, alreadyTriedFaces.at (i)); // log not satisfied on all tried faces
  }
  engine->logRejectedInterest(pitEntry, nextHop);
  clearKnownFaces(int_to_forward);
  sendNACKToAllInFaces(pitEntry);
  rejectPendingInterest(pitEntry);
}

std::vector<int> SAF::getAllInFaces(shared_ptr<pit::Entry> pitEntry)
{
  std::vector<int> faces;
  const nfd::pit::InRecordCollection records = pitEntry->getInRecords();

  for(nfd::pit::InRecordCollection::const_iterator it = records.begin (); it!=records.end (); ++it)
  {
    if(! (*it).getFace()->getScope() == ::ndn::nfd::FaceScope::FACE_SCOPE_LOCAL)
      faces.push_back((*it).getFace()->getId());
  }
  return faces;
}

void SAF::sendNACKToAllInFaces(shared_ptr<pit::Entry> pitEntry)
{

  lp::Nack nack(pitEntry->getInterest());
  nack.setReason(lp::NackReason::CONGESTION);

  const nfd::pit::InRecordCollection records = pitEntry->getInRecords();

  for(nfd::pit::InRecordCollection::const_iterator it = records.begin (); it!=records.end (); ++it)
  {
    auto face = (*it).getFace();
    if(! face->getScope() == ::ndn::nfd::FaceScope::FACE_SCOPE_LOCAL) 
    {
      face->sendNack(nack);
    }
  }
}

std::vector<int> SAF::getAllOutFaces(shared_ptr<pit::Entry> pitEntry)
{
  std::vector<int> faces;

  const pit::OutRecordCollection& records = pitEntry->getOutRecords();
  for (auto it = records.begin(); it != records.end(); it++) {
    faces.push_back((*it).getFace()->getId());
  }

  return faces;
}

bool SAF::isRtx (const nfd::Face& inFace, const ndn::Interest& interest)
{
  KnownInFaceMap::iterator it = inFaceMap.find (interest.getName ().toUri());
  if(it == inFaceMap.end ())
    return false;

  for(std::list<int>::iterator k = it->second.begin(); k != it->second.end(); k++)
  {
    if((*k) == inFace.getId ())
      return true;
  }

  return false;
}

void SAF::addToKnownInFaces(const nfd::Face& inFace, const ndn::Interest&interest)
{
  KnownInFaceMap::iterator it = inFaceMap.find (interest.getName ().toUri());

  if(it == inFaceMap.end ())
    inFaceMap[interest.getName ().toUri ()] = std::list<int>();

  std::list<int> list = inFaceMap[interest.getName ().toUri ()];

  if(std::find(list.begin (),list.end (), inFace.getId()) == list.end ()) //if face not known as in face
    inFaceMap[interest.getName ().toUri ()].push_back(inFace.getId());    //remember it
}

void SAF::clearKnownFaces(const ndn::Interest&interest)
{
  KnownInFaceMap::iterator it = inFaceMap.find (interest.getName ().toUri());

  if(it == inFaceMap.end ())
  {
    //This may happen due to bad behavior of NDN core code!
    //beforeSatisfyInterest may be called multiple times for 1 pit entry..
    return;
  }
  inFaceMap.erase (it);
}

signal::Signal< FaceTable, shared_ptr< Face > > & afterAddFace();
signal::Signal< FaceTable, shared_ptr< Face > > & beforeRemoveFace();
