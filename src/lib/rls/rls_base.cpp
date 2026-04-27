//
// This file is a part of UERANSIM project.
// Copyright (c) 2023 ALİ GÜNGÖR.
//
// https://github.com/aligungr/UERANSIM/
// See README, LICENSE, and CONTRIBUTING files for licensing details.
//

#include "rls_base.hpp"

namespace rls
{
std::string RlfCauseToString(ERlfCause cause)
{
	switch (cause)
	{
	case ERlfCause::PDU_ID_EXISTS:
		return "PDU ID already exists";
	case ERlfCause::PDU_ID_FULL:
		return "PDU ID full, cannot accept new PDUs";
	case ERlfCause::SIGNAL_LOST_TO_CONNECTED_CELL:
		return "Signal lost to the connected cell";
	}
	return "Unknown";
}
}

