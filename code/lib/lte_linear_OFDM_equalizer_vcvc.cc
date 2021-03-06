/* -*- c++ -*- */
/* 
 * Copyright 2012 Johannes Demel
 * 
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3, or (at your option)
 * any later version.
 * 
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this software; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gr_io_signature.h>
#include <lte_linear_OFDM_equalizer_vcvc.h>
#include <cstdio>
#include <cmath>


lte_linear_OFDM_equalizer_vcvc_sptr
lte_make_linear_OFDM_equalizer_vcvc (int N_rb_dl)
{
	return lte_linear_OFDM_equalizer_vcvc_sptr (new lte_linear_OFDM_equalizer_vcvc (N_rb_dl));
}


lte_linear_OFDM_equalizer_vcvc::lte_linear_OFDM_equalizer_vcvc (int N_rb_dl)
	: gr_sync_block ("linear_OFDM_equalizer_vcvc",
		gr_make_io_signature (1,1, sizeof (gr_complex)*12*N_rb_dl ),
		gr_make_io_signature (3,3, sizeof (gr_complex)*12*N_rb_dl )),
		d_cell_id(-1),
		d_N_rb_dl(N_rb_dl),
		d_sym_num(0),
		d_first_call(true),
		d_work_call(0)
		
{
    d_key=pmt::pmt_string_to_symbol("symbol");
	d_tag_id=pmt::pmt_string_to_symbol(name() );

    set_history(8);

    d_ce_vec1 = (gr_complex*)fftwf_malloc(sizeof(gr_complex)*12*N_rb_dl);
    d_ce_vec2 = (gr_complex*)fftwf_malloc(sizeof(gr_complex)*12*N_rb_dl);
    
    d_v4 = (gr_complex*)fftwf_malloc(sizeof(gr_complex)*12*N_rb_dl); 
    d_v7 = (gr_complex*)fftwf_malloc(sizeof(gr_complex)*12*N_rb_dl); //d_v7
    d_ce11 = (gr_complex*)fftwf_malloc(sizeof(gr_complex)*12*N_rb_dl);
    d_ce12 = (gr_complex*)fftwf_malloc(sizeof(gr_complex)*12*N_rb_dl);
    d_ce41 = (gr_complex*)fftwf_malloc(sizeof(gr_complex)*12*N_rb_dl);
    d_ce42 = (gr_complex*)fftwf_malloc(sizeof(gr_complex)*12*N_rb_dl);
    d_ce71 = (gr_complex*)fftwf_malloc(sizeof(gr_complex)*12*N_rb_dl);
    d_ce72 = (gr_complex*)fftwf_malloc(sizeof(gr_complex)*12*N_rb_dl);
    d_divider1_4 = (gr_complex*)fftwf_malloc(sizeof(gr_complex)*12*N_rb_dl);
    d_divider1_3 = (gr_complex*)fftwf_malloc(sizeof(gr_complex)*12*N_rb_dl);
    d_diff1 = (gr_complex*)fftwf_malloc(sizeof(gr_complex)*12*N_rb_dl);
    d_diff2 = (gr_complex*)fftwf_malloc(sizeof(gr_complex)*12*N_rb_dl);
    
    for(int i = 0 ; i < 12 * N_rb_dl ; i++){
        d_divider1_3[i] = gr_complex(1.0/3.0,0);
        d_divider1_4[i] = gr_complex(1.0/4.0,0);
    }
    
    for (int i = 0 ; i < 7 ; i++ ){
        d_erg1[i] = (gr_complex*)fftwf_malloc(sizeof(gr_complex)*12*N_rb_dl);
        d_erg2[i] = (gr_complex*)fftwf_malloc(sizeof(gr_complex)*12*N_rb_dl);
    }

    
}


lte_linear_OFDM_equalizer_vcvc::~lte_linear_OFDM_equalizer_vcvc ()
{
    fftwf_free(d_ce_vec1);
    fftwf_free(d_ce_vec2);
    fftwf_free(d_v4);
    fftwf_free(d_v7);
    fftwf_free(d_ce11);
    fftwf_free(d_ce12);
    fftwf_free(d_ce41);
    fftwf_free(d_ce42);
    fftwf_free(d_ce71);
    fftwf_free(d_ce72);
    fftwf_free(d_divider1_4);
    fftwf_free(d_divider1_3);
    fftwf_free(d_diff1);
    fftwf_free(d_diff2);
    for (int i = 0 ; i < 7 ; i++ ){
        fftwf_free(d_erg1[i]);
        fftwf_free(d_erg2[i]);
    }


}

const gr_complex
lte_linear_OFDM_equalizer_vcvc::d_C_I = gr_complex(0,1);


int
lte_linear_OFDM_equalizer_vcvc::work (int noutput_items,
			gr_vector_const_void_star &input_items,
			gr_vector_void_star &output_items)
{
	const gr_complex *in = (const gr_complex *) input_items[0];
	gr_complex *out1 = (gr_complex *) output_items[0];
    gr_complex *out2 = (gr_complex *) output_items[1];
    gr_complex *out3 = (gr_complex *) output_items[2];
    
    //printf("%s.work\tnoutput_items = %i\tnitems_read = %ld\n", name().c_str(), noutput_items, nitems_read(0) );
    
    if(d_cell_id < 0){
        return 0;
    }
    
    int cell_id = d_cell_id;
    int N_rb_dl = d_N_rb_dl;
    int sym_num = d_sym_num;
    int ns = sym_num/7;
    
    //printf("%+f\t%+f\t%+f\t%+f\t%+f\t%+f\t%+f\t%+f\n",(*(in)).real(),(*(in+72)).real(),(*(in+2*72)).real(),(*(in+3*72)).real(),(*(in+4*72)).real(),(*(in+5*72)).real(),(*(in+6*72)).real(),(*(in+7*72)).real() );
 
	
	
    // First we need to have some samples input. (vectors for 1 slot)
    if(d_first_call){
        d_first_call = false;
        //printf("first call of work\n");
        memset(out1,0,7*12*N_rb_dl*sizeof(gr_complex) );
        memset(out2,0,7*12*N_rb_dl*sizeof(gr_complex) );
        memset(out3,0,7*12*N_rb_dl*sizeof(gr_complex) );
        gr_complex vec1[12*N_rb_dl]; //= new gr_complex[12*N_rb_dl];
        memcpy(vec1,in+7*12*N_rb_dl,12*N_rb_dl*sizeof(gr_complex) );
        calc_eq_freq_domain(d_ce_vec1, vec1, d_rs_matrix[0][0]);
        calc_eq_freq_domain(d_ce_vec2, vec1, d_rs_matrix[1][0]); 
        return 7;
    }
    
    // read stream tags!
    //int my_sym_num = 0;
    std::vector <gr_tag_t> v;
    get_tags_in_range(v,0,nitems_read(0),nitems_read(0)+7);
    if(v.size() > 0){
	    // due to set_history(8) and some other stuff (skiphead etc.), the calculation for sym_num in this block has to look like this.
	    sym_num = abs( (int(pmt::pmt_to_long(v[0].value) -7 )%140) );//abs((my_sym_num-7)%140)
	}
	
	// Be careful! The following if-statement causes the equalizer to only calculate OFDM symbols which carry the MIB!
	if(sym_num > 7){
	    //printf("%s.work SKIP sym_num = %i\n", name().c_str(), sym_num);
	    return 7;
	}

    
    
    memcpy(d_v4,in+4*12*N_rb_dl,12*N_rb_dl*sizeof(gr_complex));
    memcpy(d_v7,in+7*12*N_rb_dl,12*N_rb_dl*sizeof(gr_complex));
    memcpy(d_ce11,d_ce_vec1,sizeof(gr_complex)*12*d_N_rb_dl);
    memcpy(d_ce12,d_ce_vec2,sizeof(gr_complex)*12*d_N_rb_dl);

    
    calc_eq_freq_domain(d_ce41,d_v4,d_rs_matrix[0][2*ns+1]);
    calc_eq_freq_domain(d_ce42,d_v4,d_rs_matrix[1][2*ns+1]);
    calc_eq_freq_domain(d_ce71,d_v7,d_rs_matrix[0][(2*ns+2)%40]);
    calc_eq_freq_domain(d_ce72,d_v7,d_rs_matrix[1][(2*ns+2)%40]);

    memcpy(d_erg1[0],d_ce11,sizeof(gr_complex)*12*d_N_rb_dl);
    memcpy(d_erg2[0],d_ce12,sizeof(gr_complex)*12*d_N_rb_dl);
    memcpy(d_erg1[4],d_ce41,sizeof(gr_complex)*12*d_N_rb_dl);
    memcpy(d_erg2[4],d_ce42,sizeof(gr_complex)*12*d_N_rb_dl);

/*
    // Calculate vectors for first half! 
    volk_32f_x2_subtract_32f_a((float*)d_diff1, (float*)d_erg1[4], (float*)d_erg1[0], 2*sizeof(gr_complex)*12*d_N_rb_dl);
    volk_32f_x2_subtract_32f_a((float*)d_diff2, (float*)d_erg2[4], (float*)d_erg2[0], 2*sizeof(gr_complex)*12*d_N_rb_dl);
    volk_32fc_x2_multiply_32fc_a(d_diff1,d_diff1,d_divider1_4,sizeof(gr_complex)*12*d_N_rb_dl);
    volk_32fc_x2_multiply_32fc_a(d_diff2,d_diff2,d_divider1_4,sizeof(gr_complex)*12*d_N_rb_dl);
    for(int i = 1 ; i < 4 ; i++){
        memcpy(d_erg1[i],d_erg1[0], sizeof(gr_complex)*12*d_N_rb_dl);
        memcpy(d_erg2[i],d_erg2[0], sizeof(gr_complex)*12*d_N_rb_dl);
        for(int n = 1; n <=i ; n++){
            volk_32f_x2_add_32f_a((float*)d_erg1[i],(float*)d_erg1[i],(float*)d_diff1,2*sizeof(gr_complex)*12*d_N_rb_dl);
            volk_32f_x2_add_32f_a((float*)d_erg2[i],(float*)d_erg2[i],(float*)d_diff2,2*sizeof(gr_complex)*12*d_N_rb_dl);
        }
    }
    
    // Calculate vectors for second half!   
    volk_32f_x2_subtract_32f_a((float*)d_diff1, (float*)d_ce71, (float*)d_erg1[4], 2*sizeof(gr_complex)*12*d_N_rb_dl);
    volk_32f_x2_subtract_32f_a((float*)d_diff2, (float*)d_ce72, (float*)d_erg2[4], 2*sizeof(gr_complex)*12*d_N_rb_dl);
    volk_32fc_x2_multiply_32fc_a(d_diff1,d_diff1,d_divider1_3,sizeof(gr_complex)*12*d_N_rb_dl);
    volk_32fc_x2_multiply_32fc_a(d_diff2,d_diff2,d_divider1_3,sizeof(gr_complex)*12*d_N_rb_dl);
    for(int i = 1 ; i < 3 ; i++){
        memcpy(d_erg1[i+4],d_erg1[4], sizeof(gr_complex)*12*d_N_rb_dl);
        memcpy(d_erg2[i+4],d_erg2[4], sizeof(gr_complex)*12*d_N_rb_dl);
        for(int n = 1; n <=i ; n++){
            volk_32f_x2_add_32f_a((float*)d_erg1[i],(float*)d_erg1[i],(float*)d_diff1,2*sizeof(gr_complex)*12*d_N_rb_dl);
            volk_32f_x2_add_32f_a((float*)d_erg2[i],(float*)d_erg2[i],(float*)d_diff2,2*sizeof(gr_complex)*12*d_N_rb_dl);
        }
    }
*/    
    
  
    float ma[2][2];
    float pa[2][2];
    float md[2][2];
    float pd[2][2];

    // Do interpolation between vectors.
    for (int h = 0 ; h < 12*N_rb_dl ; h++ ){
        ma[0][0]=abs(d_ce11[h]);
        ma[0][1]=abs(d_ce12[h]);
        pa[0][0]=arg(d_ce11[h]);
        pa[0][1]=arg(d_ce12[h]);
        
        ma[1][0]=abs(d_ce41[h]);
        ma[1][1]=abs(d_ce42[h]);
        pa[1][0]=arg(d_ce41[h]);
        pa[1][1]=arg(d_ce42[h]);
        
        md[0][0]=(ma[1][0]-ma[0][0])/4;
        md[0][1]=(ma[1][1]-ma[0][1])/4;
        pd[0][0]=(pa[1][0]-pa[0][0])/4;
        pd[0][1]=(pa[1][1]-pa[0][1])/4;
        
        md[1][0]=(abs(d_ce71[h])-ma[1][0] )/3;
        md[1][1]=(abs(d_ce72[h])-ma[1][1] )/3;
        pd[1][0]=(arg(d_ce71[h])-pa[1][0] )/3;
        pd[1][1]=(arg(d_ce72[h])-pa[1][1] )/3;
         
        for(int o = 1 ; o < 4 ; o ++){
            //d_erg1[o  ][h]=(ma[0][0]+md[0][0]*o)*exp(d_C_I*(pa[0][0]+pd[0][0]*o) );
            d_erg1[o  ][h] = gr_complex( (ma[0][0]+md[0][0]*o)*cos(pa[0][0]+pd[0][0]*o), (ma[0][0]+md[0][0]*o)*sin(pa[0][0]+pd[0][0]*o) );
            //d_erg2[o  ][h]=(ma[0][1]+md[0][1]*o)*exp(d_C_I*(pa[0][1]+pd[0][1]*o) );
            d_erg2[o  ][h] = gr_complex( (ma[0][1]+md[0][1]*o)*cos(pa[0][1]+pd[0][1]*o), (ma[0][1]+md[0][1]*o)*sin(pa[0][1]+pd[0][1]*o) );
        }
         
        for(int o = 1 ; o < 3 ; o ++){
            //d_erg1[o+4][h]=(ma[1][0]+md[1][0]*o)*exp(d_C_I*(pa[1][0]+pd[1][0]*o) );
            d_erg1[o+4][h]= gr_complex( (ma[1][0]+md[1][0]*o)*cos(pa[1][0]+pd[1][0]*o), (ma[1][0]+md[1][0]*o)*sin(pa[1][0]+pd[1][0]*o) );
            //d_erg2[o+4][h]=(ma[1][1]+md[1][1]*o)*exp(d_C_I*(pa[1][1]+pd[1][1]*o) );
            d_erg2[o+4][h]= gr_complex( (ma[1][1]+md[1][1]*o)*cos(pa[1][1]+pd[1][1]*o), (ma[1][1]+md[1][1]*o)*sin(pa[1][1]+pd[1][1]*o) );
        }
        
    }
    
    
    memcpy(d_ce_vec1,d_ce71,sizeof(gr_complex)*12*d_N_rb_dl);
    memcpy(d_ce_vec2,d_ce72,sizeof(gr_complex)*12*d_N_rb_dl);
    
    for (int i = 0 ; i < 7 ; i++){
        memcpy(out2+i*12*N_rb_dl,d_erg1[i], 12*N_rb_dl*sizeof(gr_complex));
        memcpy(out3+i*12*N_rb_dl,d_erg2[i], 12*N_rb_dl*sizeof(gr_complex));
    }
    memcpy(out1,in,7*12*N_rb_dl*sizeof(gr_complex));

    d_work_call++;
    	
    // set d_sym_num to new value
    if (sym_num+7 < 140){sym_num+=7;}
    else {sym_num = 0;}
    d_sym_num = sym_num;
    
    // required delete calls to prevent method from sucking up all memory

    
	// Tell runtime system how many output items we produced.
	return 7;

}


void
lte_linear_OFDM_equalizer_vcvc::calc_eq_freq_domain(gr_complex* ce, gr_complex* svec, gr_complex* rvec)
{

    int N_rb_dl = d_N_rb_dl;
    
    int first_rs=0;
    int last_rs=0;
    
    for(int j = 0 ; j < 12*N_rb_dl; j++ ){
        if (abs(rvec[j]) != 0){
            gr_complex tmp = svec[j]/rvec[j];
            float mag = abs(tmp);
            float phase = arg(tmp);
            if (j > 6){
                float arg_diff = arg(svec[j-6]/rvec[j-6]);
                while( (phase-arg_diff) > M_PI){
                    phase=phase-2*M_PI;
                }
                while( (phase-arg_diff) < -M_PI){
                    phase=phase+2*M_PI;
                }
                last_rs=j;
            }
            else{
                first_rs=j;
            }
            //ce[j]=mag*exp(d_C_I*phase);
            ce[j]=mag * gr_complex(cos(phase),sin(phase));
            
            if(j > 6){
                float absce6=abs(ce[j-6]);
                float argce6=arg(ce[j-6]);
                float magdiff   = (abs(ce[j]) - absce6)/6;
                float phasediff = (arg(ce[j]) - argce6)/6;
                for (int k = 1; k < 6 ; k++){
                    //ce[j-6+k]=(absce6+k*magdiff)*exp(d_C_I*(argce6+k*phasediff) );
                    ce[j-6+k] = gr_complex( (absce6+k*magdiff)*cos(argce6+k*phasediff), (absce6+k*magdiff)*sin(argce6+k*phasediff) );
                }
            }
        }
    }
    
    //calculate CEs before first RS
    float magdiff   = (abs(ce[first_rs+6])-abs(ce[first_rs]) )/6;
    float phasediff = (arg(ce[first_rs+6])-arg(ce[first_rs]) )/6;
    float magc = abs(ce[first_rs]);
    float phasec = arg(ce[first_rs]);
    for(int m = 0; m < first_rs ; m++){
        //ce[m]=(magc-magdiff*(first_rs-m) )*exp(d_C_I*(phasec-phasediff*(first_rs-m)));
        ce[m]= gr_complex((magc-magdiff*(first_rs-m))*cos(phasec-phasediff*(first_rs-m)), (magc-magdiff*(first_rs-m))*sin(phasec-phasediff*(first_rs-m)) );
    }
    
    //calculate CEs after last RS
    magdiff   = (abs(ce[last_rs])-abs(ce[last_rs-6]) )/6;
    phasediff = (arg(ce[last_rs])-arg(ce[last_rs-6]) )/6;
    magc   = abs(ce[last_rs]);
    phasec = arg(ce[last_rs]);
    for(int m = 1; m < 12*N_rb_dl-last_rs ; m++){
        //ce[last_rs+m]=(magc+magdiff*m )*exp(d_C_I*(phasec+phasediff*m ));
        ce[last_rs+m]=gr_complex( (magc+magdiff*m)*cos(phasec+phasediff*m), (magc+magdiff*m)*sin(phasec+phasediff*m) );
    }

    
    //return ce;
}


void
lte_linear_OFDM_equalizer_vcvc::pn_seq_generator(char* c, int len, int cinit)
{

	
	const int Nc=1600; //Constant is defined in standard
	
	char x2[3*len+Nc];
	for (int i = 0; i<31; i++){
	    	char val = cinit%2;
    		cinit = floor(cinit/2);
    		x2[i] = val;	
	}

	char x1[3*len+Nc];
	// initialization for first 35 elements is needed. (Otherwise READ BEFORE INITIALIZATION ERROR)
	for (int i = 0 ; i < 35 ; i++){
		x1[i]=0;
	}
	x1[0]=1;

	for (int n=0; n <2*len+Nc-3;n++){
		x1[n+31]=(x1[n+3]+x1[n])%2;
		x2[n+31]=(x2[n+3]+x2[n+2]+x2[n+1]+x2[n])%2;
	}

	for (int n=0;n<len;n++){
		c[n]=(x1[n+Nc]+x2[n+Nc])%2;
	}

}

// Be careful! This method returns a complex array with 220 elements. There is no way to specify it or do other stuff.
void //gr_complex*
lte_linear_OFDM_equalizer_vcvc::rs_generator(gr_complex* r, int ns,int l,int cell_id,int Ncp)
{
    
    const float SQRT2 = sqrt(2.0);
    const int Nrbmax = 110;
    int cinit = 1024*(7*(ns+1)+l+1)*(2*cell_id+1)+2*cell_id+Ncp;
    
    char c[4*Nrbmax];
    pn_seq_generator(c, 4*Nrbmax,cinit);
    
    //gr_complex *r=new gr_complex[220];
    //gr_complex r[220];
    //gr_complex r(2.0,3.0);
    for (int m = 0 ; m < 2*Nrbmax ; m++ ){
        r[m]=gr_complex( (1-2*c[2*m])/SQRT2 , (1-2*c[2*m+1])/SQRT2 );
    }

}

gr_complex* 
lte_linear_OFDM_equalizer_vcvc::rs_mapper(int N_rb_dl, int ns,int l,int cell_id,int Ncp,int p)
{
    const int Nrbdlmax = 110;
    
    gr_complex r[2*Nrbdlmax];
    rs_generator(r, ns, l, cell_id, Ncp);
    int vshift = cell_id%6;
    
    int v = 0;
    if       (p==0 && l==0){v=0;}
    else if (p==0 && l!=0){v=3;}
    else if (p==1 && l==0){v=3;}
    else if (p==1 && l!=0){v=0;}
    else if (p==2){v=3*(ns%2);}
    else if (p==3){v=3*3*(ns%2);}
    
    gr_complex *vec = new gr_complex[N_rb_dl*12];
    memset(vec,0,12*N_rb_dl*sizeof(gr_complex) );
    
    int mX = 0;
    int k = 0;
    for (int m = 0 ; m < 2*N_rb_dl ; m++ ){
        mX = m+Nrbdlmax-N_rb_dl;
        k  = 6*m + ((v+vshift)%6);
        vec[k] = r[mX];
    }

    return vec;
}

void
lte_linear_OFDM_equalizer_vcvc::set_cell_id(int id)
{
    printf("%s\tset_cell_id = %i\n", name().c_str(), id);
    d_cell_id = id;
    int cell_id = id;
    int Ncp = 1;
    int N_rb_dl = d_N_rb_dl;
    for (int p = 0 ; p < 2 ; p++ ){
        for (int ns = 0 ; ns < 20 ; ns++ ){
            d_rs_matrix[p][2*ns  ]=rs_mapper(N_rb_dl,ns,0,cell_id,Ncp,p);
            d_rs_matrix[p][2*ns+1]=rs_mapper(N_rb_dl,ns,4,cell_id,Ncp,p);
        }
    }

}





