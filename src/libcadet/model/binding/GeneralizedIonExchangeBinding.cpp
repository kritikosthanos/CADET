// =============================================================================
//  CADET - The Chromatography Analysis and Design Toolkit
//  
//  Copyright © 2008-2020: The CADET Authors
//            Please see the AUTHORS and CONTRIBUTORS file.
//  
//  All rights reserved. This program and the accompanying materials
//  are made available under the terms of the GNU Public License v3.0 (or, at
//  your option, any later version) which accompanies this distribution, and
//  is available at http://www.gnu.org/licenses/gpl.html
// =============================================================================

#include "model/binding/BindingModelBase.hpp"
#include "model/ExternalFunctionSupport.hpp"
#include "model/binding/BindingModelMacros.hpp"
#include "model/binding/RefConcentrationSupport.hpp"
#include "model/ModelUtils.hpp"
#include "cadet/Exceptions.hpp"
#include "model/Parameters.hpp"
#include "LocalVector.hpp"
#include "SimulationTypes.hpp"

#include <functional>
#include <unordered_map>
#include <string>
#include <vector>

/*<codegen>
{
	"name": "GIEXParamHandler",
	"externalName": "ExtGIEXParamHandler",
	"parameters":
		[
			{ "type": "ScalarParameter", "varName": "lambda", "confName": "GIEX_LAMBDA"},
			{ "type": "ScalarComponentDependentParameter", "varName": "kA", "confName": "GIEX_KA"},
			{ "type": "ScalarComponentDependentParameter", "varName": "kALin", "confName": "GIEX_KA_LIN"},
			{ "type": "ScalarComponentDependentParameter", "varName": "kAQuad", "confName": "GIEX_KA_QUAD"},
			{ "type": "ScalarComponentDependentParameter", "varName": "kASalt", "confName": "GIEX_KA_SALT"},
			{ "type": "ScalarComponentDependentParameter", "varName": "kAProt", "confName": "GIEX_KA_PROT"},
			{ "type": "ScalarComponentDependentParameter", "varName": "kD", "confName": "GIEX_KD"},
			{ "type": "ScalarComponentDependentParameter", "varName": "kDLin", "confName": "GIEX_KD_LIN"},
			{ "type": "ScalarComponentDependentParameter", "varName": "kDQuad", "confName": "GIEX_KD_QUAD"},
			{ "type": "ScalarComponentDependentParameter", "varName": "kDSalt", "confName": "GIEX_KD_SALT"},
			{ "type": "ScalarComponentDependentParameter", "varName": "kDProt", "confName": "GIEX_KD_PROT"},
			{ "type": "ScalarComponentDependentParameter", "varName": "nu", "confName": "GIEX_NU"},
			{ "type": "ScalarComponentDependentParameter", "varName": "nuLin", "confName": "GIEX_NU_LIN"},
			{ "type": "ScalarComponentDependentParameter", "varName": "nuQuad", "confName": "GIEX_NU_QUAD"},
			{ "type": "ScalarComponentDependentParameter", "varName": "sigma", "confName": "GIEX_SIGMA"}
		],
	"constantParameters":
		[
			{ "type": "ReferenceConcentrationParameter", "varName": ["refC0", "refQ"], "objName": "_refConcentration", "confPrefix": "GIEX_"}
		]
}
</codegen>*/

/* Parameter description
 ------------------------
 lambda = Ionic capacity
 kA = Adsorption rate
 kD = Desorption rate
 nu = Characteristic charge
 sigma = Steric factor
 refC0, refQ = Reference concentrations
*/

namespace cadet
{

namespace model
{

inline const char* GIEXParamHandler::identifier() CADET_NOEXCEPT { return "GENERALIZED_ION_EXCHANGE"; }

inline bool GIEXParamHandler::validateConfig(unsigned int nComp, unsigned int const* nBoundStates)
{
	if (nComp <= 2)
		throw InvalidParameterException("GENERALIZED_ION_EXCHANGE requires at least 3 components");

	if (_kA.size() < nComp)
		throw InvalidParameterException("GIEX_KA requires NCOMP entries");
	if (_kD.size() < nComp)
		throw InvalidParameterException("GIEX_KD requires NCOMP entries");
	if (_nu.size() < nComp)
		throw InvalidParameterException("GIEX_NU requires NCOMP entries");
	if (_sigma.size() < nComp)
		throw InvalidParameterException("GIEX_SIGMA requires NCOMP entries");

	return true;
}

inline const char* ExtGIEXParamHandler::identifier() CADET_NOEXCEPT { return "EXT_GENERALIZED_ION_EXCHANGE"; }

inline bool ExtGIEXParamHandler::validateConfig(unsigned int nComp, unsigned int const* nBoundStates)
{
	if (nComp <= 2)
		throw InvalidParameterException("EXT_GENERALIZED_ION_EXCHANGE requires at least 3 components");

	if (_kA.size() < nComp)
		throw InvalidParameterException("EXT_GIEX_KA requires NCOMP entries");
	if (_kD.size() < nComp)
		throw InvalidParameterException("EXT_GIEX_KD requires NCOMP entries");
	if (_nu.size() < nComp)
		throw InvalidParameterException("EXT_GIEX_NU requires NCOMP entries");
	if (_sigma.size() < nComp)
		throw InvalidParameterException("EXT_GIEX_SIGMA requires NCOMP entries");

	return true;
}


/**
 * @brief Defines the generalized ion exchange binding model
 * @details Implements the generalized ion exchange binding model, which is based on the steric mass action model: \f[ \begin{align} 
 *              q_0 &= \Lambda - \sum_{j \geq 2} \nu_j(\mathrm{pH}) q_j \\
 *              \frac{\partial q_i}{\partial t} &= k_{a,i}(c_p, q, \mathrm{pH}) \left(\Lambda - \sum_{j \geq 2} \left(\nu_j(\mathrm{pH}) + \sigma_{j}\right) q_j \right)^{\nu_i(\mathrm{pH})} c_{p,i} - k_{d,i}(c_p, q, \mathrm{pH}) c_{p,0}^{\nu_i(pH)} q_i \\
 *              \nu_i(\mathrm{pH}) &= \nu_{i,0} + \mathrm{pH} \nu_{i,1} + \mathrm{pH}^2 \nu_{i,2} \\
 *              k_{a,i}\left(c_p, q, \mathrm{pH}\right) &= k_{a,i,0} \exp\left(k_{a,i,1} \mathrm{pH} + k_{a,i,2} \mathrm{pH}^2 + k_{a,i,\mathrm{salt}} c_{p,0} + k_{a,i,\mathrm{prot}} c_{p,i}\right) \\
 *              k_{d,i}\left(c_p, q, \mathrm{pH}\right) &= k_{d,i,0} \exp\left(k_{d,i,1} \mathrm{pH} + k_{d,i,2} \mathrm{pH}^2 + k_{d,i,\mathrm{salt}} c_{p,0} + k_{d,i,\mathrm{prot}} c_{p,i}\right)
 *          \end{align} \f]
 *          Component @c 0 is assumed to be salt. Component @c 1 is a second non-binding modifier component (e.g., pH).
 *          Multiple bound states are not supported. Components without bound state (i.e., non-binding components) 
 *          are supported.
 *          
 *          See @cite Huuk2017.
 * @tparam ParamHandler_t Type that can add support for external function dependence
 */
template <class ParamHandler_t>
class GeneralizedIonExchangeBindingBase : public ParamHandlerBindingModelBase<ParamHandler_t>
{
public:

	GeneralizedIonExchangeBindingBase() { }
	virtual ~GeneralizedIonExchangeBindingBase() CADET_NOEXCEPT { }

	static const char* identifier() { return ParamHandler_t::identifier(); }

	virtual bool configureModelDiscretization(IParameterProvider& paramProvider, unsigned int nComp, unsigned int const* nBound, unsigned int const* boundOffset)
	{
		const bool res = BindingModelBase::configureModelDiscretization(paramProvider, nComp, nBound, boundOffset);

		// Guarantee that salt has exactly one bound state
		if (nBound[0] != 1)
			throw InvalidParameterException("Generalized ion exchange binding model requires exactly one bound state for salt component");

		// Guarantee that modifier component is non-binding
		if (nBound[1] != 0)
			throw InvalidParameterException("Generalized ion exchange binding model requires non-binding modifier component (NBOUND[1] = 0)");

		// First flux is salt, which is always quasi-stationary
		_reactionQuasistationarity[0] = true;

		return res;
	}

	virtual bool hasSalt() const CADET_NOEXCEPT { return true; }
	virtual bool supportsMultistate() const CADET_NOEXCEPT { return false; }
	virtual bool supportsNonBinding() const CADET_NOEXCEPT { return true; }
	virtual bool hasQuasiStationaryReactions() const CADET_NOEXCEPT { return true; }
	virtual bool implementsAnalyticJacobian() const CADET_NOEXCEPT { return true; }

	virtual bool preConsistentInitialState(double t, unsigned int secIdx, const ColumnPosition& colPos, double* y, double const* yCp, LinearBufferAllocator workSpace) const
	{
		typename ParamHandler_t::ParamsHandle const p = _paramHandler.update(t, secIdx, colPos, _nComp, _nBoundStates, workSpace);

		// Compute salt component from given bound states q_j

		// Pseudo component 1 is pH
		const double pH = yCp[1];

		// Salt equation: q_0 - Lambda + Sum[nu_j(pH) * q_j, j] == 0
		//           <=>  q_0 == Lambda - Sum[nu_j(pH) * q_j, j]
		y[0] = static_cast<double>(p->lambda);

		unsigned int bndIdx = 1;
		for (int j = 2; j < _nComp; ++j)
		{
			// Skip components without bound states (bound state index bndIdx is not advanced)
			if (_nBoundStates[j] == 0)
				continue;

			const double nu_j = static_cast<double>(p->nu[j]) + pH * (static_cast<double>(p->nuLin[j]) + pH * static_cast<double>(p->nuQuad[j]));

			y[0] -= nu_j * y[bndIdx];

			// Next bound component
			++bndIdx;
		}

		return true;
	}
	
	virtual void postConsistentInitialState(double t, unsigned int secIdx, const ColumnPosition& colPos, double* y, double const* yCp, LinearBufferAllocator workSpace) const
	{
		preConsistentInitialState(t, secIdx, colPos, y, yCp, workSpace);
	}


	CADET_BINDINGMODELBASE_BOILERPLATE

protected:
	using ParamHandlerBindingModelBase<ParamHandler_t>::_paramHandler;
	using ParamHandlerBindingModelBase<ParamHandler_t>::_reactionQuasistationarity;
	using ParamHandlerBindingModelBase<ParamHandler_t>::_nComp;
	using ParamHandlerBindingModelBase<ParamHandler_t>::_nBoundStates;

	template <typename StateType, typename CpStateType, typename ResidualType, typename ParamType>
	int fluxImpl(double t, unsigned int secIdx, const ColumnPosition& colPos, StateType const* y,
		CpStateType const* yCp, ResidualType* res, LinearBufferAllocator workSpace) const
	{
		using CpStateParamType = typename DoubleActivePromoter<CpStateType, ParamType>::type;
		using StateParamType = typename DoubleActivePromoter<StateType, ParamType>::type;

		typename ParamHandler_t::ParamsHandle const p = _paramHandler.update(t, secIdx, colPos, _nComp, _nBoundStates, workSpace);

		// Pseudo component 1 is pH
		const CpStateType pH = yCp[1];

		// Salt flux: q_0 - Lambda + Sum[nu_j * q_j, j] == 0 
		//       <=>  q_0 == Lambda - Sum[nu_j * q_j, j] 
		// Also compute \bar{q}_0 = q_0 - Sum[sigma_j * q_j, j]
		res[0] = y[0] - static_cast<ParamType>(p->lambda);
		StateParamType q0_bar = y[0];

		unsigned int bndIdx = 1;
		for (int j = 2; j < _nComp; ++j)
		{
			// Skip components without bound states (bound state index bndIdx is not advanced)
			if (_nBoundStates[j] == 0)
				continue;

			const CpStateParamType nu_j = static_cast<ParamType>(p->nu[j]) + pH * (static_cast<ParamType>(p->nuLin[j]) + pH * static_cast<ParamType>(p->nuQuad[j]));

			res[0] += nu_j * y[bndIdx];
			q0_bar -= static_cast<ParamType>(p->sigma[j]) * y[bndIdx];

			// Next bound component
			++bndIdx;
		}

		const ParamType refC0 = static_cast<ParamType>(p->refC0);
		const ParamType refQ = static_cast<ParamType>(p->refQ);
		const CpStateParamType yCp0_divRef = yCp[0] / refC0;
		const StateParamType q0_bar_divRef = q0_bar / refQ;

		// Protein fluxes: -k_{a,i}(c_p, q, \mathrm{pH}) * \bar{q}_0^{\nu_i(\mathrm{pH})} * c_{p,i} + k_{d,i}(c_p, q, \mathrm{pH}) * c_{p,0}^{\nu_i(pH)} * q_i
		bndIdx = 1;
		for (int i = 2; i < _nComp; ++i)
		{
			// Skip components without bound states (bound state index bndIdx is not advanced)
			if (_nBoundStates[i] == 0)
				continue;

			const CpStateParamType nu_i = static_cast<ParamType>(p->nu[i]) + pH * (static_cast<ParamType>(p->nuLin[i]) + pH * static_cast<ParamType>(p->nuQuad[i]));
			const CpStateParamType c0_pow_nu = pow(yCp0_divRef, nu_i);
			const StateParamType q0_bar_pow_nu = pow(q0_bar_divRef, nu_i);
			
			// k_{a,i}(c_p, q, \mathrm{pH}) = k_{a,i,0} \exp(k_{a,i,1} \mathrm{pH} + k_{a,i,2} \mathrm{pH}^2 + k_{a,i,\mathrm{salt}} c_{p,0} + k_{a,i,\mathrm{prot}} c_{p,i})
			const CpStateParamType ka_i = static_cast<ParamType>(p->kA[i]) * 
				exp(pH * (static_cast<ParamType>(p->kALin[i]) + pH * static_cast<ParamType>(p->kAQuad[i])) 
					+ static_cast<ParamType>(p->kASalt[i]) * yCp0_divRef + static_cast<ParamType>(p->kAProt[i]) * yCp[i]
				);
			const CpStateParamType kd_i = static_cast<ParamType>(p->kD[i]) * 
				exp(pH * (static_cast<ParamType>(p->kDLin[i]) + pH * static_cast<ParamType>(p->kDQuad[i])) 
					+ static_cast<ParamType>(p->kDSalt[i]) * yCp0_divRef + static_cast<ParamType>(p->kDProt[i]) * yCp[i]
				);

			// Residual
			res[bndIdx] = kd_i * y[bndIdx] * c0_pow_nu - ka_i * yCp[i] * q0_bar_pow_nu;

			// Next bound component
			++bndIdx;
		}

		return 0;
	}

	template <typename RowIterator>
	void jacobianImpl(double t, unsigned int secIdx, const ColumnPosition& colPos, double const* y, double const* yCp, int offsetCp, RowIterator jac, LinearBufferAllocator workSpace) const
	{
		typename ParamHandler_t::ParamsHandle const p = _paramHandler.update(t, secIdx, colPos, _nComp, _nBoundStates, workSpace);

		// Pseudo component 1 is pH
		const double pH = yCp[1];

		double q0_bar = y[0];

		// Getting to c_{p,0}: -bndIdx takes us to q_0, another -offsetCp to c_{p,0}. This means jac[-bndIdx - offsetCp] corresponds to c_{p,0}.
		// Getting to c_{p,i}: -bndIdx takes us to q_0, another -offsetCp to c_{p,0} and a +i to c_{p,i}.
		//                     This means jac[i - bndIdx - offsetCp] corresponds to c_{p,i}.

		// Salt flux: q_0 - Lambda + Sum[nu_j * q_j, j] == 0
		jac[0] = 1.0;
		int bndIdx = 1;
		for (int j = 2; j < _nComp; ++j)
		{
			// Skip components without bound states (bound state index bndIdx is not advanced)
			if (_nBoundStates[j] == 0)
				continue;

			const double nu_j = static_cast<double>(p->nu[j]) + pH * (static_cast<double>(p->nuLin[j]) + pH * static_cast<double>(p->nuQuad[j]));

			jac[bndIdx] = nu_j;
			jac[1 - offsetCp] += (static_cast<double>(p->nuLin[j]) + 2.0 * pH * static_cast<double>(p->nuQuad[j])) * y[bndIdx];

			// Calculate \bar{q}_0 = q_0 - Sum[sigma_j * q_j, j]
			q0_bar -= static_cast<double>(p->sigma[j]) * y[bndIdx];

			// Next bound component
			++bndIdx;
		}

		// Advance to protein fluxes
		++jac;

		const double refC0 = static_cast<double>(p->refC0);
		const double refQ = static_cast<double>(p->refQ);
		const double yCp0_divRef = yCp[0] / refC0;
		const double q0_bar_divRef = q0_bar / refQ;

		// Protein fluxes: -k_{a,i} * c_{p,i} * \bar{q}_0^{nu_i} + k_{d,i} * q_i * c_{p,0}^{nu_i}
		// We have already computed \bar{q}_0 in the loop above
		bndIdx = 1;
		for (int i = 2; i < _nComp; ++i)
		{
			// Skip components without bound states (bound state index bndIdx is not advanced)
			if (_nBoundStates[i] == 0)
				continue;

			const double ka = static_cast<double>(p->kA[i]);
			const double kd = static_cast<double>(p->kD[i]);
			const double nu = static_cast<double>(p->nu[i]) + pH * (static_cast<double>(p->nuLin[i]) + pH * static_cast<double>(p->nuQuad[i]));
			const double dNuDpH = static_cast<double>(p->nuLin[i]) + 2.0 * pH * static_cast<double>(p->nuQuad[i]);

			const double c0_pow_nu     = pow(yCp0_divRef, nu);
			const double q0_bar_pow_nu = pow(q0_bar_divRef, nu);
			const double c0_pow_nu_m1_divRef     = pow(yCp0_divRef, nu - 1.0) / refC0;
			const double q0_bar_pow_nu_m1_divRef = nu * pow(q0_bar_divRef, nu - 1.0) / refQ;

			// k_{a,i}(c_p, q, \mathrm{pH}) = k_{a,i,0} \exp(k_{a,i,1} \mathrm{pH} + k_{a,i,2} \mathrm{pH}^2 + k_{a,i,\mathrm{salt}} c_{p,0} + k_{a,i,\mathrm{prot}} c_{p,i})
			const double ka_i = ka * 
				exp(pH * (static_cast<double>(p->kALin[i]) + pH * static_cast<double>(p->kAQuad[i])) 
					+ static_cast<double>(p->kASalt[i]) * yCp0_divRef + static_cast<double>(p->kAProt[i]) * yCp[i]
				);
			const double dKaDpH = ka_i * (static_cast<double>(p->kALin[i]) + 2.0 * pH * static_cast<double>(p->kAQuad[i]));
			const double kd_i = kd * 
				exp(pH * (static_cast<double>(p->kDLin[i]) + pH * static_cast<double>(p->kDQuad[i])) 
					+ static_cast<double>(p->kDSalt[i]) * yCp0_divRef + static_cast<double>(p->kDProt[i]) * yCp[i]
				);
			const double dKdDpH = kd_i * (static_cast<double>(p->kDLin[i]) + 2.0 * pH * static_cast<double>(p->kDQuad[i]));

			// dres_i / dc_{p,0}
			jac[-bndIdx - offsetCp] = kd_i * y[bndIdx] * (nu * c0_pow_nu_m1_divRef + c0_pow_nu * static_cast<double>(p->kDSalt[i]) / refC0) - ka_i * yCp[i] * q0_bar_pow_nu * static_cast<double>(p->kASalt[i]) / refC0;
			// dres_i / dc_{p,1}
			jac[1 - bndIdx - offsetCp] = y[bndIdx] * c0_pow_nu * (dKdDpH + kd_i * std::log(yCp0_divRef) * dNuDpH) - yCp[i] * q0_bar_pow_nu * (dKaDpH + ka_i * std::log(q0_bar_divRef) * dNuDpH);
			// dres_i / dc_{p,i}
			jac[i - bndIdx - offsetCp] = -ka_i * q0_bar_pow_nu * (1.0 + yCp[i] * static_cast<double>(p->kAProt[i])) + kd_i * y[bndIdx] * c0_pow_nu * static_cast<double>(p->kDProt[i]);
			// dres_i / dq_0
			jac[-bndIdx] = -ka_i * yCp[i] * q0_bar_pow_nu_m1_divRef;

			// Fill dres_i / dq_j
			int bndIdx2 = 1;
			for (int j = 2; j < _nComp; ++j)
			{
				// Skip components without bound states (bound state index bndIdx is not advanced)
				if (_nBoundStates[j] == 0)
					continue;

				// dres_i / dq_j
				jac[bndIdx2 - bndIdx] = -ka_i * yCp[i] * q0_bar_pow_nu_m1_divRef * (-static_cast<double>(p->sigma[j]));
				// Getting to q_j: -bndIdx takes us to q_0, another +bndIdx2 to q_j. This means jac[bndIdx2 - bndIdx] corresponds to q_j.

				++bndIdx2;
			}

			// Add to dres_i / dq_i
			jac[0] += kd_i * c0_pow_nu;

			// Advance to next flux and Jacobian row
			++bndIdx;
			++jac;
		}
	}
};


typedef GeneralizedIonExchangeBindingBase<GIEXParamHandler> GeneralizedIonExchangeBinding;
typedef GeneralizedIonExchangeBindingBase<ExtGIEXParamHandler> ExternalGeneralizedIonExchangeBinding;

namespace binding
{
	void registerGeneralizedIonExchangeModel(std::unordered_map<std::string, std::function<model::IBindingModel*()>>& bindings)
	{
		bindings[GeneralizedIonExchangeBinding::identifier()] = []() { return new GeneralizedIonExchangeBinding(); };
		bindings[ExternalGeneralizedIonExchangeBinding::identifier()] = []() { return new ExternalGeneralizedIonExchangeBinding(); };
	}
}  // namespace binding

}  // namespace model

}  // namespace cadet
