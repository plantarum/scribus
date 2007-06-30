/*
For general Scribus (>=1.3.2) copyright and licensing information please refer
to the COPYING file provided with the program. Following this notice may exist
a copyright and/or license notice that predates the release of Scribus 1.3.2
for which a new license (GPL+exception) is in place.
*/


#include "scdocoutput.h"

bool ScDocOutput::makeOutput(ScribusDoc* doc, vector<int>& pageNumbers)
{
 bool  done = true;
 Page* page;

	begin();

	for ( uint index = 0; index < pageNumbers.size(); index++ )
	{
		page = doc->Pages->at( pageNumbers[index] - 1 );
		ScPageOutput* outputComponent = createPageOutputComponent(index + 1);
		if( outputComponent != NULL )
		{
			outputComponent->begin();
			outputComponent->DrawPage(page);
			outputComponent->end();
		}
		else
			done = false;
		if (outputComponent)
			delete outputComponent;
		if (!done)
			break;
	}

	end();

	return done;
}
