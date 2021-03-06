
#include "locals.hpp"

#include "esmreader.hpp"
#include "esmwriter.hpp"

void ESM::Locals::load (ESMReader &esm)
{
    while (esm.isNextSub ("LOCA"))
    {
        std::string id = esm.getHString();

        Variant value;
        value.read (esm, Variant::Format_Info);

        mVariables.push_back (std::make_pair (id, value));
    }
}

void ESM::Locals::save (ESMWriter &esm) const
{
    for (std::vector<std::pair<std::string, Variant> >::const_iterator iter (mVariables.begin());
        iter!=mVariables.end(); ++iter)
    {
        esm.writeHNString ("LOCA", iter->first);
        iter->second.write (esm, Variant::Format_Info);
    }
}