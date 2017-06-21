
#include "TaskMgr.h"
#include "Funcs.h"
#include "Thread.h"
#include "Log.h"

H_BNAMSP

SINGLETON_INIT(CTaskMgr)
CTaskMgr objTaskMgr;

CTaskMgr::CTaskMgr(void) : m_usThreadNum(H_INIT_NUMBER), m_pRunner(NULL)
{
    
}

CTaskMgr::~CTaskMgr(void)
{
    for (taskit itTask = m_mapTask.begin(); m_mapTask.end() != itTask; ++itTask)
    {
        H_SafeDelete(itTask->second);
    }
    m_mapTask.clear();

    H_SafeDelArray(m_pRunner);
}

void CTaskMgr::setThreadNum(const unsigned short usNum)
{
    m_usThreadNum = ((H_INIT_NUMBER == usNum) ? H_GetCoreCount() * 2 : usNum);
    m_pRunner = new(std::nothrow) CTaskRunner[m_usThreadNum];
    H_ASSERT(NULL != m_pRunner, "malloc memory error.");

    for (unsigned short usI = H_INIT_NUMBER; usI < m_usThreadNum; ++usI)
    {
        m_pRunner[usI].setIndex(usI);
        CThread::Creat(&m_pRunner[usI]);
        m_pRunner[usI].waitStart();
    }
}

CChan *CTaskMgr::getChan(const char *pszTaskName)
{
    H_ASSERT(NULL != pszTaskName, "got null pointer.");
    CChan *pChan(NULL);

    m_objTaskLock.rLock();
    taskit itTask = m_mapTask.find(std::string(pszTaskName));
    if (m_mapTask.end() != itTask)
    {
        pChan = itTask->second->getChan();
    }
    m_objTaskLock.unLock();

    return pChan;
}

void CTaskMgr::taskRPCCall(unsigned int &uiId, const char *pszRPCName, const char *pszToTask, const char *pszSrcTask,
    const char *pMsg, const size_t &iLens)
{
    H_ASSERT(NULL != pszRPCName 
        && NULL != pszToTask 
        && NULL != pszSrcTask, "got null pointer.");
    CChan *pChan(getChan(pszToTask));
    if (NULL == pChan)
    {
        return;
    }

    H_RPC stRPC;
    H_Zero(&stRPC, sizeof(stRPC));
    stRPC.uiId = uiId;
    stRPC.uiMsgLens = (unsigned int)iLens;
    memcpy(stRPC.acRPC, pszRPCName, strlen(pszRPCName));
    memcpy(stRPC.acSrcTask, pszSrcTask, strlen(pszSrcTask));
    memcpy(stRPC.acToTask, pszToTask, strlen(pszToTask));

    H_MSG *pChanMsg = new(std::nothrow) H_MSG;
    H_ASSERT(NULL != pChanMsg, "malloc memory error.");
    pChanMsg->usEnevt = MSG_TASK_RPCCALL;
    pChanMsg->pEvent = new(std::nothrow) char[sizeof(H_RPC) + iLens];
    H_ASSERT(NULL != pChanMsg->pEvent, "malloc memory error.");
    memcpy(pChanMsg->pEvent, &stRPC, sizeof(stRPC));
    if (NULL != pMsg)
    {
        memcpy(pChanMsg->pEvent + sizeof(stRPC), pMsg, iLens);
    }    

    if (!pChan->Send(pChanMsg))
    {
        H_SafeDelArray(pChanMsg->pEvent);
        H_SafeDelete(pChanMsg);

        H_LOG(LOGLV_ERROR, "%s", "add message to CirQueue error.");
    }
}

void CTaskMgr::regTask(CTaskWorker *pTask)
{
    m_objTaskLock.wLock();
    taskit itTask = m_mapTask.find(*pTask->getName());
    H_ASSERT(m_mapTask.end() == itTask, "task repeat register.");
    m_mapTask.insert(std::make_pair(*pTask->getName(), pTask));
    m_objTaskLock.unLock();

    notifyInit(pTask);

    m_objAllNamLock.wLock();
    m_lstAllName.push_back(*pTask->getName());
    m_objAllNamLock.unLock();
}

void CTaskMgr::unregTask(const char *pszName)
{
    H_ASSERT(NULL != pszName, "got null pointer.");
    CTaskWorker *pTask(NULL);
    std::string strName(pszName);

    m_objTaskLock.wLock();
    taskit itTask = m_mapTask.find(strName);
    if (m_mapTask.end() != itTask)
    {
        pTask = itTask->second;
        m_mapTask.erase(itTask);
    }
    m_objTaskLock.unLock();

    if (NULL == pTask)
    {
        H_LOG(LOGLV_WARN, "not find task by name %s", pszName);
        return;
    }

    notifyDestroy(pTask);

    m_objAllNamLock.wLock();
    for (std::list<std::string>::iterator itName = m_lstAllName.begin(); m_lstAllName.end() != itName; itName++)
    {
        if (*itName == strName)
        {
            m_lstAllName.erase(itName);
            break;
        }
    }
    m_objAllNamLock.unLock();
}

void CTaskMgr::stopWorker(void)
{
    for (unsigned short usI = H_INIT_NUMBER; usI < m_usThreadNum; ++usI)
    {
        m_pRunner[usI].Join();
    }

    for (taskit itTask = m_mapTask.begin(); m_mapTask.end() != itTask; ++itTask)
    {
        itTask->second->destroyTask();
    }
}

std::vector<std::string> CTaskMgr::getAllName(void)
{
    std::vector<std::string> vcTmp;

    m_objAllNamLock.rLock();
    for (std::list<std::string>::iterator itName = m_lstAllName.begin(); m_lstAllName.end() != itName; itName++)
    {
        vcTmp.push_back(*itName);
    }
    m_objAllNamLock.unLock();

    return vcTmp;
}

H_ENAMSP