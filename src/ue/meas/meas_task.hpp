//
//

#pragma once

#include "meas_provider.hpp"

#include <cstdint>
#include <unordered_map>
#include <vector>

#include <lib/rls/rls_pdu.hpp>
#include <lib/udp/server.hpp>
#include <ue/types.hpp>
#include <utils/nts.hpp>

namespace nr::ue
{

class MeasTask : public NtsTask
{
  private:
    TaskBase *m_base;
    std::unique_ptr<Logger> m_logger;
    AllCellMeasurements *allMeas;
    MeasSourceConfig *measConfig;

    // ptr to measurement provider service for out-of-band measurements (e.g. from RLS or file)
    std::unique_ptr<MeasurementProvider> m_measProvider;

    friend class UeCmdHandler;

  public:
    explicit MeasTask(TaskBase *base);
    ~MeasTask() override = default;

  protected:
    void onStart() override;
    void onLoop() override;
    void onQuit() override;


};

} // namespace nr::ue
