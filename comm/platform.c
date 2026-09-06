#include "idef.h"




ines_char_t* get_file_title(ines_char_t* title, ines_cstr_t  file_path)
{
	ines_cstr_t  from, to;

	from = file_path;
	to = NULL;

	while(*file_path)
	{
		if(*file_path == '\\' || *file_path == '/')
		{
			from = file_path+1;
			to = NULL;
		}
		else if(*file_path == '.' && from != NULL && to == NULL)
		{
			to = file_path;
		}
		file_path++;
	}

	if(to == NULL)
		to = file_path;

	while(from < to)
	{
		*title++ = *from++;
	}
	*title = 0;
	
	return title;
}

