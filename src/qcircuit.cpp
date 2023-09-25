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

#include <iomanip>

namespace Qrack {

std::ostream& operator<<(std::ostream& os, const QCircuitGatePtr g)
{
    os << (size_t)g->target << " ";

    os << g->controls.size() << " ";
    for (const bitLenInt& c : g->controls) {
        os << (size_t)c << " ";
    }

    os << g->payloads.size() << " ";
#if FPPOW > 6
    os << std::setprecision(36);
#elif FPPOW > 5
    os << std::setprecision(17);
#endif
    for (const auto& p : g->payloads) {
        os << p.first << " ";
        for (size_t i = 0U; i < 4U; ++i) {
            os << p.second.get()[i] << " ";
        }
    }

    return os;
}

std::istream& operator>>(std::istream& is, QCircuitGatePtr& g)
{
    g->payloads.clear();

    size_t target;
    is >> target;
    g->target = (bitLenInt)target;

    size_t cSize;
    is >> cSize;
    for (size_t i = 0U; i < cSize; ++i) {
        size_t c;
        is >> c;
        g->controls.insert((bitLenInt)c);
    }

    size_t pSize;
    is >> pSize;
    for (size_t i = 0U; i < pSize; ++i) {
        bitCapInt k;
        is >> k;

        g->payloads[k] = std::shared_ptr<complex>(new complex[4], std::default_delete<complex[]>());
        for (size_t j = 0U; j < 4U; ++j) {
            is >> g->payloads[k].get()[j];
        }
    }

    return is;
}

std::ostream& operator<<(std::ostream& os, const QCircuitPtr c)
{
    os << (size_t)c->GetQubitCount() << " ";

    std::list<QCircuitGatePtr> gates = c->GetGateList();
    os << gates.size() << " ";
    for (const QCircuitGatePtr& g : gates) {
        os << g;
    }

    return os;
}

std::istream& operator>>(std::istream& is, QCircuitPtr& c)
{
    size_t qubitCount;
    is >> qubitCount;
    c->SetQubitCount((bitLenInt)qubitCount);

    size_t gSize;
    is >> gSize;
    std::list<QCircuitGatePtr> gl;
    for (size_t i = 0U; i < gSize; ++i) {
        QCircuitGatePtr g = std::make_shared<QCircuitGate>();
        is >> g;
        gl.push_back(g);
    }
    c->SetGateList(gl);

    return is;
}

void QCircuit::AppendGate(QCircuitGatePtr nGate)
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    std::lock_guard<std::recursive_mutex> oLock(nGate->mutex);

    if (!isCollapsed) {
        gates.push_back(nGate);
        InitReverse();
        return;
    }

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

    for (std::list<QCircuitGatePtr>::reverse_iterator gateIt = gates.rbegin(); gateIt != gates.rend(); ++gateIt) {
        const QCircuitGatePtr gate = *gateIt;
        if (gate->TryCombine(nGate)) {
            if (gate->IsIdentity()) {
                std::list<QCircuitGatePtr> head(gateIt.base(), gates.end());
                gates.erase(gateIt.base(), gates.end());
                for (std::list<QCircuitGatePtr>::iterator gIt = head.begin(); gIt != head.end(); ++gIt) {
                    const QCircuitGatePtr g = *gIt;
                    if (!nGate->CanCombine(g) && !nGate->CanPass(g)) {
                        gates.push_back(g);
                        InitReverse();
                    } else {
                        AppendGate(g);
                    }
                }
            }

            InitReverse();
            return;
        }
        if (!gate->CanPass(nGate)) {
            gates.insert(gateIt.base(), { nGate });
            InitReverse();
            return;
        }
    }

    gates.push_front(nGate);
    InitReverse();
}

void QCircuit::Run(QInterfacePtr qsim)
{
    std::lock_guard<std::recursive_mutex> lock(mutex);

    if (qsim->GetQubitCount() < qubitCount) {
        qsim->Allocate(qubitCount - qsim->GetQubitCount());
    }

    std::list<QCircuitGatePtr> nGates;
    if (gates.size() < 3U) {
        nGates = gates;
    } else {
        std::list<QCircuitGatePtr>::iterator end = gates.begin();
        std::advance(end, gates.size() - 2U);
        std::list<QCircuitGatePtr>::iterator gate;
        for (gate = gates.begin(); gate != end; ++gate) {
            if (!(*gate)->IsCnot()) {
                nGates.push_back(*gate);
                continue;
            }
            std::list<QCircuitGatePtr>::iterator adv = gate;
            ++adv;
            if (!(*adv)->IsCnot() || ((*adv)->target != *((*gate)->controls.begin())) ||
                ((*gate)->target != *((*adv)->controls.begin()))) {
                nGates.push_back(*gate);
                continue;
            }
            ++adv;
            if (!(*adv)->IsCnot() || ((*adv)->target != (*gate)->target) ||
                (*((*gate)->controls.begin()) != *((*adv)->controls.begin()))) {
                nGates.push_back(*gate);
                continue;
            }
            nGates.push_back(std::make_shared<QCircuitGate>((*gate)->target, *((*gate)->controls.begin())));
            gate = adv;
            if (std::distance(gate, gates.end()) < 3) {
                ++gate;
                break;
            }
        }
        for (; gate != gates.end(); ++gate) {
            nGates.push_back(*gate);
        }
    }

    for (const QCircuitGatePtr& gate : nGates) {
        const bitLenInt& t = gate->target;

        if (!gate->controls.size()) {
            qsim->Mtrx(gate->payloads[0].get(), t);

            continue;
        }

        std::vector<bitLenInt> controls = gate->GetControlsVector();

        if (!gate->payloads.size()) {
            qsim->Swap(controls[0U], t);

            continue;
        }

        if (gate->payloads.size() == 1U) {
            const auto& payload = gate->payloads.begin();
            qsim->UCMtrx(controls, payload->second.get(), t, payload->first);

            continue;
        }

        std::unique_ptr<complex[]> payload = gate->MakeUniformlyControlledPayload();
        qsim->UniformlyControlledSingleBit(controls, t, payload.get());
    }
}
} // namespace Qrack
