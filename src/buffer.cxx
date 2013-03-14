#include <cstdio>
#include <cmath>
#include "input.h"
#include "grid.h"
#include "fields.h"
#include "buffer.h"
#include "defines.h"

cbuffer::cbuffer(cgrid *gridin, cfields *fieldsin, cmpi *mpiin)
{
  grid   = gridin;
  fields = fieldsin;
  mpi    = mpiin;

  allocated = false;
}

cbuffer::~cbuffer()
{
  if(allocated)
    for (std::map<std::string,double*>::iterator it = bufferprofs.begin(); it!=bufferprofs.end(); it++)
      delete it->second;
}

int cbuffer::readinifile(cinput *inputin)
{
  int n = 0;

  // optional parameters
  n += inputin->getItem(&ibuffer,      "fields", "ibuffer"     , 0 );
  n += inputin->getItem(&bufferkstart, "fields", "bufferkstart", 0 );
  n += inputin->getItem(&buffersigma,  "fields", "buffersigma" , 2.);
  n += inputin->getItem(&bufferbeta,   "fields", "bufferbeta"  , 2.);

    // if one argument fails, then crash
  if(n > 0)
    return 1;

  return 0;
}

int cbuffer::init()
{
  if(ibuffer == 1)
  {
    // allocate the buffer array 
    bufferkcells = grid->kmax-bufferkstart-1;

    for(fieldmap::iterator itProg = fields->MomentumProg.begin(); itProg!=fields->MomentumProg.end(); itProg++)
      bufferprofs[itProg->first] = new double[bufferkcells];

    for(fieldmap::iterator itProg = fields->ScalarProg.begin(); itProg!=fields->ScalarProg.end(); itProg++)
      bufferprofs[itProg->first] = new double[bufferkcells];

    allocated = true;

    // add the ghost cells to the starting point
    bufferkstart += grid->kstart;
  }

  return 0;
}

int cbuffer::setbuffers()
{
  if(ibuffer)
  {
    // set the buffers according to the initial profiles
    for(fieldmap::iterator itProg = fields->MomentumProg.begin(); itProg!=fields->MomentumProg.end(); itProg++)
      setbuffer((*itProg->second).data, bufferprofs[itProg->first]);
 
    for(fieldmap::iterator itProg = fields->ScalarProg.begin(); itProg!=fields->ScalarProg.end(); itProg++)
      setbuffer((*itProg->second).data, bufferprofs[itProg->first]);
  }

  return 0;
}

int cbuffer::exec()
{
  if(ibuffer)
  {
    // calculate the buffer tendencies
    buffer((*fields->MomentumTend["u"]).data, (*fields->MomentumProg["u"]).data, bufferprofs["u"], grid->z );
    buffer((*fields->MomentumTend["v"]).data, (*fields->MomentumProg["v"]).data, bufferprofs["v"], grid->z );
    buffer((*fields->MomentumTend["w"]).data, (*fields->MomentumProg["w"]).data, bufferprofs["w"], grid->zh );
 
    for(fieldmap::iterator itProg = fields->ScalarProg.begin(); itProg!=fields->ScalarProg.end(); itProg++)
      buffer((*fields->ScalarTend[itProg->first]).data, (*itProg->second).data, bufferprofs[itProg->first], grid->z );
  }

  return 0;
}

int cbuffer::buffer(double * restrict at, double * restrict a, double * restrict abuf, double * restrict z)
{ 
  int ijk,jj,kk;
  int kloopstart;

  jj = grid->icells;
  kk = grid->icells*grid->jcells;

  kloopstart = bufferkstart+1;

  double sigma;
  double zsizebuf;

  zsizebuf = grid->zsize - z[bufferkstart];

  for(int k=kloopstart; k<grid->kend; k++)
  {
    sigma = buffersigma*std::pow((z[k]-z[bufferkstart])/zsizebuf, bufferbeta);
    for(int j=grid->jstart; j<grid->jend; j++)
#pragma ivdep
      for(int i=grid->istart; i<grid->iend; i++)
      {
        ijk = i + j*jj + k*kk;
        at[ijk] -= sigma*(a[ijk]-abuf[k-kloopstart]);
      }
  }

  return 0;
}

int cbuffer::setbuffer(double * restrict a, double * restrict abuf)
{
  int ijk,jj,kk;
  int kloopstart;

  jj = grid->icells;
  kk = grid->icells*grid->jcells;

  kloopstart = bufferkstart+1;

  for(int k=kloopstart; k<grid->kend; k++)
  {
    abuf[k-kloopstart] = 0.;
    for(int j=grid->jstart; j<grid->jend; j++)
#pragma ivdep
      for(int i=grid->istart; i<grid->iend; i++)
      {
        ijk = i + j*jj + k*kk;
        abuf[k-kloopstart] += a[ijk];
      }

    abuf[k-kloopstart] /= grid->imax*grid->jmax;
  }

  grid->getprof(abuf, bufferkcells);

  return 0;
}

int cbuffer::save()
{
  if(ibuffer != 1)
    return 0;

  char filename[256];
  std::sprintf(filename, "%s.%07d", "buffer", 0);

  if(mpi->mpiid == 0)
  {
    std::printf("Saving \"%s\"\n", filename);
    FILE *pFile;
    pFile = fopen(filename, "wb");

    if(pFile == NULL)
    {
      std::printf("ERROR \"%s\" cannot be written", filename);
      return 1;
    }

    for (std::map<std::string,double*>::iterator itBuffer = bufferprofs.begin(); itBuffer!=bufferprofs.end(); itBuffer++)
      fwrite(itBuffer->second, sizeof(double), bufferkcells, pFile);
    
    fclose(pFile);
  }

  return 0;
}

int cbuffer::load()
{
  if(ibuffer != 1)
    return 0;

  char filename[256];
  std::sprintf(filename, "%s.%07d", "buffer", 0);

  if(mpi->mpiid == 0)
  {
    std::printf("Loading \"%s\"\n", filename);

    FILE *pFile;
    pFile = fopen(filename, "rb");

    if(pFile == NULL)
    {
      std::printf("ERROR \"%s\" does not exist\n", filename);
      return 1;
    }

    for (std::map<std::string,double*>::iterator itBuffer = bufferprofs.begin(); itBuffer!=bufferprofs.end(); itBuffer++)
      fread(itBuffer->second, sizeof(double), bufferkcells, pFile);
    
    fclose(pFile);
  }

  // send the buffers to all processes
  for (std::map<std::string,double*>::iterator itBuffer = bufferprofs.begin(); itBuffer!=bufferprofs.end(); itBuffer++)
    mpi->broadcast(itBuffer->second, bufferkcells);

  return 0;
}
