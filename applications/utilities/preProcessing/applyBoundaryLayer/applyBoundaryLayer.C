/*---------------------------------------------------------------------------*\
  =========                 |
  \\      /  F ield         | OpenFOAM: The Open Source CFD Toolbox
   \\    /   O peration     |
    \\  /    A nd           | Copyright (C) 2011-2012 OpenFOAM Foundation
     \\/     M anipulation  |
-------------------------------------------------------------------------------
License
    This file is part of OpenFOAM.

    OpenFOAM is free software: you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    OpenFOAM is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
    for more details.

    You should have received a copy of the GNU General Public License
    along with OpenFOAM.  If not, see <http://www.gnu.org/licenses/>.

Application
    applyBoundaryLayer

Description
    Apply a simplified boundary-layer model to the velocity and
    turbulence fields based on the 1/7th power-law.

    The uniform boundary-layer thickness is either provided via the -ybl option
    or calculated as the average of the distance to the wall scaled with
    the thickness coefficient supplied via the option -Cbl.  If both options
    are provided -ybl is used.

\*---------------------------------------------------------------------------*/

#include "fvCFD.H"
#include "singlePhaseTransportModel.H"
#include "RASModel.H"
#include "wallDist.H"

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

// turbulence constants - file-scope
static const scalar Cmu(0.09);
static const scalar kappa(0.41);


int main(int argc, char *argv[])
{
    argList::addNote
    (
        "apply a simplified boundary-layer model to the velocity and\n"
        "turbulence fields based on the 1/7th power-law."
    );

    argList::addOption
    (
        "ybl",
        "scalar",
        "specify the boundary-layer thickness"
    );
    argList::addOption
    (
        "Cbl",
        "scalar",
        "boundary-layer thickness as Cbl * mean distance to wall"
    );
    argList::addBoolOption
    (
        "writenut",
        "write nut field"
    );

    #include "setRootCase.H"

    if (!args.optionFound("ybl") && !args.optionFound("Cbl"))
    {
        FatalErrorIn(args.executable())
            << "Neither option 'ybl' or 'Cbl' have been provided to calculate "
            << "the boundary-layer thickness.\n"
            << "Please choose either 'ybl' OR 'Cbl'."
            << exit(FatalError);
    }
    else if (args.optionFound("ybl") && args.optionFound("Cbl"))
    {
        FatalErrorIn(args.executable())
            << "Both 'ybl' and 'Cbl' have been provided to calculate "
            << "the boundary-layer thickness.\n"
            << "Please choose either 'ybl' OR 'Cbl'."
            << exit(FatalError);
    }

    #include "createTime.H"
    #include "createMesh.H"
    #include "createFields.H"

    // * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

    Info<< "Time = " << runTime.timeName() << nl << endl;

    // Modify velocity by applying a 1/7th power law boundary-layer
    // u/U0 = (y/ybl)^(1/7)
    // assumes U0 is the same as the current cell velocity

    scalar yblv = ybl.value();
    forAll(U, celli)
    {
        if (y[celli] <= yblv)
        {
            U[celli] *= ::pow(y[celli]/yblv, (1.0/7.0));
        }
    }

    Info<< "Writing U\n" << endl;
    U.write();

    // Update/re-write phi
    phi = fvc::interpolate(U) & mesh.Sf();
    phi.write();


    if (isA<incompressible::RASModel>(turbulence()))
    {
        // Calculate nut
        tmp<volScalarField> tnut = turbulence->nut();
        volScalarField& nut = tnut();
        volScalarField S(mag(dev(symm(fvc::grad(U)))));
        nut = sqr(kappa*min(y, ybl))*::sqrt(2)*S;

        if (args.optionFound("writenut"))
        {
            Info<< "Writing nut" << endl;
            nut.write();
        }

        // Create G field - used by RAS wall functions
        volScalarField G("RASModel::G", nut*2*sqr(S));


        //--- Read and modify turbulence fields

        // Turbulence k
        tmp<volScalarField> tk = turbulence->k();
        volScalarField& k = tk();
        scalar ck0 = pow025(Cmu)*kappa;
        k = sqr(nut/(ck0*min(y, ybl)));
        k.correctBoundaryConditions();

        Info<< "Writing k\n" << endl;
        k.write();


        // Turbulence epsilon

        tmp<volScalarField> tepsilon = turbulence->epsilon();
        volScalarField& epsilon = tepsilon();
        scalar ce0 = ::pow(Cmu, 0.75)/kappa;
        epsilon = ce0*k*sqrt(k)/min(y, ybl);
        epsilon.correctBoundaryConditions();

        Info<< "Writing epsilon\n" << endl;
        epsilon.write();

        // Turbulence omega
        IOobject omegaHeader
        (
            "omega",
            runTime.timeName(),
            mesh,
            IOobject::MUST_READ,
            IOobject::NO_WRITE,
            false
        );

        if (omegaHeader.headerOk())
        {
            volScalarField omega(omegaHeader, mesh);
            omega =
                epsilon
               /(
                   Cmu*k+dimensionedScalar("VSMALL", k.dimensions(), VSMALL)
                );
            omega.correctBoundaryConditions();

            Info<< "Writing omega\n" << endl;
            omega.write();
        }

        // Turbulence nuTilda
        IOobject nuTildaHeader
        (
            "nuTilda",
            runTime.timeName(),
            mesh,
            IOobject::MUST_READ,
            IOobject::NO_WRITE,
            false
        );

        if (nuTildaHeader.headerOk())
        {
            volScalarField nuTilda(nuTildaHeader, mesh);
            nuTilda = nut;
            nuTilda.correctBoundaryConditions();

            Info<< "Writing nuTilda\n" << endl;
            nuTilda.write();
        }
    }

    Info<< nl << "ExecutionTime = " << runTime.elapsedCpuTime() << " s"
        << "  ClockTime = " << runTime.elapsedClockTime() << " s"
        << nl << endl;

    Info<< "End\n" << endl;

    return 0;
}


// ************************************************************************* //
