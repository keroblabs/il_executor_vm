#include "critical_section.h"
//#include "drivers.h"

//////////////////////////////////////////////////////////////////////////////////////

static volatile uint32_t critical_counter = 0;

//////////////////////////////////////////////////////////////////////////////////////

void critical_section_init(void)
{
    critical_counter = 0;
//    intr_set_enabled(TRUE);
//    intr_global_set_enabled(TRUE);
}

void critical_section_start(void)
{
//    if (intr_is_servicing())
//    {
//        // do not disable interrupts while servicing an interrupt
//        return;
//    }

    if (critical_counter == 0)
    {
        //intr_set_enabled(FALSE);
    }

    critical_counter++;
}

void critical_section_end(void)
{
//    if (intr_is_servicing())
//    {
//        // do not enable interrupts while servicing an interrupt
//        return;
//    }

    if (critical_counter > 0)
    {
        critical_counter--;

        if (critical_counter == 0)
        {
            //intr_set_enabled(TRUE);
        }
    }
    else
    {
        // this should never happen, it is an unbalanced critical section end
    }
}
