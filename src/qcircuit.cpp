//////////////////////////////////////////////////////////////////////////////////////
//
// (C) Daniel Strano and the Qrack contributors 2017-2023. All rights reserved.
//
// This is a multithreaded, universal quantum register simulation, allowing
// (nonphysical) register cloning and direct measurement of probability and
// phase, to leverage what advantages classical emulation of qubits can have.
//
// Licensed under the GNU Lesser General Public License V3.
// See LICENSE.md in the project root or https://www.gnu.org/licenses/lgpl-3.0.en.html
// for details.

#include "qcircuit.hpp"

namespace Qrack {

void QCircuit::AppendGate(QCircuitGatePtr nGate)
{
    if (nGate->IsIdentity()) {
        return;
    }

    if ((nGate->target + 1U) > qubitCount) {
        qubitCount = nGate->target + 1U;
    }
    if (!(nGate->controls.empty())) {
        const bitLenInt q = *(nGate->controls.rbegin());
        if ((q + 1U) > qubitCount) {
            qubitCount = (q + 1U);
        }
    }

    for (std::list<QCircuitGatePtr>::reverse_iterator gate = gates.rbegin(); gate != gates.rend(); ++gate) {
        if ((*gate)->TryCombine(nGate)) {
            if ((*gate)->IsIdentity()) {
                std::list<QCircuitGatePtr>::reverse_iterator _gate = gate++;
                std::list<QCircuitGatePtr> head(_gate.base(), gates.end());
                gates.erase(gate.base(), gates.end());
                for (std::list<QCircuitGatePtr>::iterator g = head.begin(); g != head.end(); ++g) {
                    if (!nGate->CanCombine(*g) && !nGate->CanPass(*g)) {
                        gates.push_back(*g);
                    } else {
                        AppendGate(*g);
                    }
                }
            }
            return;
        }
        if (!(*gate)->CanPass(nGate)) {
            gates.insert(gate.base(), { nGate });
            return;
        }
    }

    gates.push_front(nGate);
}

void QCircuit::Run(QInterfacePtr qsim)
{
    if (qsim->GetQubitCount() < qubitCount) {
        qsim->Allocate(qubitCount - qsim->GetQubitCount());
    }

    std::vector<bool> controlStates(qubitCount, false);
    for (const QCircuitGatePtr& gate : gates) {
        const bitLenInt& t = gate->target;

        if (!gate->controls.size()) {
            if (controlStates[t]) {
                const std::unique_ptr<complex[]> m = InvertPayload(gate->payloads[0].get());
                qsim->Mtrx(m.get(), t);
                controlStates[t] = false;
            } else {
                qsim->Mtrx(gate->payloads[0].get(), t);
            }

            continue;
        }

        std::vector<bitLenInt> controls = gate->GetControlsVector();

        if ((gate->payloads.size() == (1U << controls.size())) || (gate->payloads.size() >= 8)) {
            for (const bitLenInt& c : controls) {
                if (controlStates[c]) {
                    qsim->X(c);
                    controlStates[c] = false;
                }
            }
            if (controlStates[t]) {
                qsim->X(t);
                controlStates[t] = false;
            }

            std::unique_ptr<complex[]> payload = gate->MakeUniformlyControlledPayload();
            qsim->UniformlyControlledSingleBit(controls, t, payload.get());

            continue;
        }

        for (const auto& payload : gate->payloads) {
            std::map<bitLenInt, bool> controlMismatch;
            size_t mismatchCount = 0;
            for (bitLenInt i = 0; i < controls.size(); ++i) {
                const bitLenInt c = controls[i];
                controlMismatch[c] = (((bool)((payload.first >> i) & 1)) != controlStates[c]);
                if (controlMismatch[c]) {
                    ++mismatchCount;
                }
            }

            if (controlStates[t]) {
                qsim->X(t);
                controlStates[t] = false;
            }

            if ((mismatchCount << 1U) > controls.size()) {
                for (const bitLenInt& c : controls) {
                    if (!controlMismatch[c]) {
                        qsim->X(c);
                        controlStates[c] = !controlStates[c];
                    }
                }

                qsim->MCMtrx(controls, payload.second.get(), t);

                continue;
            }

            for (const bitLenInt& c : controls) {
                if (controlMismatch[c]) {
                    qsim->X(c);
                    controlStates[c] = !controlStates[c];
                }
            }

            qsim->MACMtrx(controls, payload.second.get(), t);
        }
    }

    for (size_t i = 0U; i < controlStates.size(); ++i) {
        if (controlStates[i]) {
            qsim->X(i);
        }
    }
}
} // namespace Qrack