TerrainMesh::TerrainMesh(QuadData* qd, float segSize, float startX, float startY,
                         const Ogre::Vector3 &pos,
                         int width, int depth, bool skirts,
                         Ogre::SceneNode *parent)
  : Ogre::Renderable(),
    Ogre::MovableObject(),
    mWidth(width),
    mUseSkirts(skirts),
    mBuilt(false),
    mDepth(depth),
    mVertexes(0),
    mIndices(0),
    mLODMorphFactor(0),
    mTextureFadeFactor(0),
    mMin(30000),
    mMax(-30000),
    mExtraMorphAmount(0),
    mHasFadePass(false),
    mQuadData(qd),
    mSegmentSize(segSize),
    mX(startX),
    mY(startY)
{
  // From QuadSegment()
  assert(qd);
  assert(segSize>0&&segSize<=1);
  assert(mY>=0&&mY<=1);
  assert(mX>=0&&mY<=1);

#ifdef QSDEBUG
  {
    //check sizes
    const float qw = mQuadData->getVertexWidth()-1;
    const float fsw = qw*segSize;
    const int isw = (int)fsw;
    assert(fsw==isw);
  }
#endif

  //precalc offsets, as getVertex/getNormal get called a lot (1000s of times)
  computeOffsets();

  // From Quad
  node = parent->createChildSceneNode(pos);

  // From create()
  createVertexBuffer();
  calculateVetexValues();
  calculateIndexValues();
  setBounds();

  node->attachObject(this);

  createMaterial();

  if ( g_heightMap->isMorphingEnabled() && mDepth != g_heightMap->getMaxDepth() ) {
    Ogre::Technique* tech = getMaterial()->getTechnique(0);
    for ( size_t i = 0; i < tech->getNumPasses(); ++i ) {
      assert(g_heightMap->isMorphingEnabled());
      tech->getPass(i)->setVertexProgram(MORPH_VERTEX_PROGRAM);
    }
  }

  if ( g_heightMap->isMorphingEnabled() )
    calculateDeltaValues();

  mBuilt = true;
}

void TerrainMesh::destroy() {
  if ( !mBuilt ) return;

  //deleting null values is fine iirc
  delete mIndices;

#   if ENABLED_CRASHING == 1
  {
    delete mVertexes;
  }
#   else
  {
    if ( mDepth != g_heightMap->getMaxDepth() ){
      delete mVertexes;
    }
  }
#   endif

  mBuilt = false;
}
//----------------------------------------------
void TerrainMesh::update(Ogre::Real time, Ogre::Real camDist, Ogre::Real usplitDist, Ogre::Real morphDist)
{
  TRACE("TerrainMesh::update");
  //if ( USE_MORPH ){

  //as aprocesh mUnsplitDistance, lower detail
  if ( camDist > morphDist && mDepth > 1 ) {
    mLODMorphFactor = 1 - (usplitDist - camDist)/(usplitDist-morphDist);
  } else
    mLODMorphFactor = 0;
  mTextureFadeFactor = mLODMorphFactor;


  //on an split, it sets the extra morph amount to 1, we then ensure this ends up at 0... slowly
  if ( mExtraMorphAmount > 0 ) {
    mLODMorphFactor += mExtraMorphAmount;
    mExtraMorphAmount -= (time/g_heightMap->getMorphSpeed()); //decrease slowly
  }
  if ( mExtraFadeAmount > 0 ) {
    mTextureFadeFactor += mExtraFadeAmount;
    mExtraFadeAmount -= (time/g_heightMap->getTextureFadeSpeed());
  }

  //Ensure within valid bounds
  if ( mLODMorphFactor > 1 )
    mLODMorphFactor = 1;
  else if ( mLODMorphFactor < 0 )
    mLODMorphFactor = 0;

  if ( mTextureFadeFactor > 1 )
    mTextureFadeFactor = 1;
  else if ( mTextureFadeFactor < 0 )
    mTextureFadeFactor = 0;

  //}

  //remove pass. Keep outside in case terrain fading is removed while it is active
  if ( mHasFadePass && mTextureFadeFactor == 0 ) {
    removeFadePass();
  } else if ( g_heightMap->isTextureFadingEnabled() &&
              !mHasFadePass &&
              mTextureFadeFactor > 0 &&
              hasParentTexture() ) {
    addFadePass();
  }

}
//----------------------------------------------
void TerrainMesh::addFadePass() {
  assert(mHasFadePass==false);

  if ( mDepth == g_heightMap->getMaxDepth() ) return;


  mHasFadePass = true;
  Ogre::MaterialPtr mat = getMaterial();
  Ogre::Pass* newPass = mat->getTechnique(0)->createPass();
  newPass->setSceneBlending(Ogre::SBF_SOURCE_ALPHA, Ogre::SBF_ONE_MINUS_SOURCE_ALPHA);

  //set fragment program
  assert(g_heightMap->isTextureFadingEnabled());
  newPass->setFragmentProgram(FADE_FRAGMENT_PROGRAM);

  if ( g_heightMap->isMorphingEnabled() && mDepth != g_heightMap->getMaxDepth() ) {
    assert(g_heightMap->isMorphingEnabled());
    newPass->setVertexProgram(MORPH_VERTEX_PROGRAM);
  }


  //set texture to be used
  newPass->createTextureUnitState(getParentTexture(), 1);
}
//----------------------------------------------
void TerrainMesh::removeFadePass() {
  assert(mHasFadePass==true);
  mHasFadePass = false;
  Ogre::MaterialPtr mat = getMaterial();
  Ogre::Technique* tech = mat->getTechnique(0);

  tech->removePass(tech->getNumPasses()-1);
}
//----------------------------------------------
void TerrainMesh::justSplit() {
  mExtraMorphAmount = 1;
  mLODMorphFactor = 1;
  mTextureFadeFactor = 1;
  mExtraFadeAmount = 1;

  if ( g_heightMap->isTextureFadingEnabled() && hasParentTexture() )
    addFadePass();
}

//----------------------------------------------
void TerrainMesh::_updateCustomGpuParameter(
                                            const GpuProgramParameters::AutoConstantEntry& constantEntry,
                                            GpuProgramParameters* params) const {
  using namespace Ogre;
  if (constantEntry.data == MORPH_CUSTOM_PARAM_ID)
    params->_writeRawConstant(constantEntry.physicalIndex, mLODMorphFactor);
  else if ( constantEntry.data == FADE_CUSTOM_PARAM_ID )
    params->_writeRawConstant(constantEntry.physicalIndex, mTextureFadeFactor);
  else
    Renderable::_updateCustomGpuParameter(constantEntry, params);


}
//----------------------------------------------
float TerrainMesh::getVertexHeight(int x, int y) {
  return getVertex(x,y);
}
//----------------------------------------------
Ogre::Vector3 TerrainMesh::getVertexPosition(int x, int y) {
  return Ogre::Vector3(x*getVertexSeperation(), getVertexHeight(x,y) , y*getVertexSeperation());
}
//----------------------------------------------
void TerrainMesh::calculateVetexValues() {
  using namespace Ogre;

  //get the texture offsets for the higher uv
  float xUVOffset = 0;
  float yUVOffset = 0;

  if ( g_heightMap->isTextureFadingEnabled() ) {
    assert(0);
  }
  /*
    switch (mInterface->getLocation()) {
    case Quad::QL_NW :
    yUVOffset = 32.0f/64.0f;
    break;
    case Quad::QL_NE:
    yUVOffset = 32.0f/64.0f;
    xUVOffset = 32.0f/64.0f;
    break;
    case Quad::QL_SE:
    xUVOffset = 32.0f/64.0f;
    break;
    default:
    break;
    }
  */

  int start = 0;
  int end = mWidth;

  if ( mUseSkirts ) {
    --start;
    ++end;
  }

  float* verts = static_cast<float*>(mMainBuffer->lock(HardwareBuffer::HBL_DISCARD));
  for ( int y = start; y < end; y++ ) {
    for ( int x = start; x < end; x++ ) {

      //the skirts
      if ( y < 0 || y > (mWidth-1) || x < 0 || x > (mWidth-1) ) {

        if ( x < 0 )		*verts++ = 0;
        else if ( x > (mWidth-1) )	*verts++ = (mWidth-1)*getVertexSeperation();
        else				*verts++ = x*getVertexSeperation();

        *verts++ = -4096; //2048 below base sea floor height

        if ( y < 0 )		        *verts++ = 0;
        else if ( y > (mWidth-1) )	*verts++ = (mWidth-1)*getVertexSeperation();
        else        				*verts++ = y*getVertexSeperation();


        for ( Ogre::uint i = 0; i < 3; i++ )
          *verts++ = 0;

        float u = (float)(x) / (mWidth-1);
        float v = (float)(y) / (mWidth-1);

        //clamped, so shouldn't matter if > 1

        *verts++ = u;
        *verts++ = v;

        if ( g_heightMap->isTextureFadingEnabled() ) {
          *verts++ = u;
          *verts++ = v;
        }
      } else {

        assert(y*mWidth+x>=0&&y*mWidth+x<mWidth*mWidth);

        //verts
        *verts++ = x*getVertexSeperation();
        *verts++ = getVertexHeight(x,y);
        *verts++ = y*getVertexSeperation();

        mMax = std::max(mMax, getVertexHeight(x,y));
        mMin = std::min(mMin, getVertexHeight(x,y));

        //normals
        for ( Ogre::uint i = 0; i < 3; i++ )
          *verts++ = getNormal(x,y,i);
        //*verts++ = mInterface->getNormal((y*mWidth+x)*3+i);

        const float u = (float)(x) / (mWidth-1);
        const float v = (float)(y) / (mWidth-1);
        assert(u>=0&&v>=0);
        assert(u<=1&&v<=1);

        *verts++ = u;
        *verts++ = v;

        if ( g_heightMap->isTextureFadingEnabled() ) {
          *verts++ = u/2.0f + xUVOffset;
          *verts++ = v/2.0f + yUVOffset;
        }
      }
    }
  }
  mMainBuffer->unlock();
}
//----------------------------------------------
void TerrainMesh::setBounds() {
  mBounds.setExtents(0,mMin,0,
                     (mWidth - 1) * getVertexSeperation(),
                     mMax,
                     (mWidth - 1) * getVertexSeperation());

  mCenter = Ogre::Vector3( ( (mWidth  - 1) * getVertexSeperation() ) / 2,
                           ( mMin + mMax ) / 2,
                           ( (mWidth - 1) * getVertexSeperation() ) / 2);

  mBoundingRadius = (mBounds.getMaximum() - mBounds.getMinimum()).length() / 2;
}
//----------------------------------------------
void TerrainMesh::createVertexBuffer() {
  using namespace Ogre;

  size_t vw = mWidth;
  if ( mUseSkirts ) vw += 2;

  mVertexes = new VertexData();
  mVertexes->vertexStart = 0;
  mVertexes->vertexCount = vw*vw;// VERTEX_WIDTH;

  VertexDeclaration* vertexDecl = mVertexes->vertexDeclaration;
  size_t currOffset = 0;

  vertexDecl->addElement(MAIN_BINDING, currOffset, VET_FLOAT3, VES_POSITION);
  currOffset += VertexElement::getTypeSize(VET_FLOAT3);

  vertexDecl->addElement(MAIN_BINDING, currOffset, VET_FLOAT3, VES_NORMAL);
  currOffset += VertexElement::getTypeSize(VET_FLOAT3);


  vertexDecl->addElement(MAIN_BINDING, currOffset, VET_FLOAT2, VES_TEXTURE_COORDINATES, 0);
  currOffset += VertexElement::getTypeSize(VET_FLOAT2);

  if ( g_heightMap->isTextureFadingEnabled() ) {
    vertexDecl->addElement(MAIN_BINDING, currOffset, VET_FLOAT2, Ogre::VES_TEXTURE_COORDINATES, 1);
    currOffset += VertexElement::getTypeSize(VET_FLOAT2);
  }

  mMainBuffer = HardwareBufferManager::getSingleton().createVertexBuffer(
                                                                         vertexDecl->getVertexSize(0), // size of one whole vertex
                                                                         mVertexes->vertexCount, // number of vertices
                                                                         HardwareBuffer::HBU_STATIC_WRITE_ONLY, // usage
                                                                         false); // no shadow buffer

  mVertexes->vertexBufferBinding->setBinding(MAIN_BINDING, mMainBuffer); //bind the data

  if ( g_heightMap->isMorphingEnabled() )
    vertexDecl->addElement(DELTA_BINDING, 0, VET_FLOAT1, VES_BLEND_WEIGHTS);


}

Ogre::HardwareVertexBufferSharedPtr TerrainMesh::createDeltaBuffer( ) {
  size_t vw = mWidth;
  if ( mUseSkirts ) vw += 2;

  const size_t totalVerts = (vw * vw);
  return Ogre::HardwareBufferManager::getSingleton().createVertexBuffer(
                                                                        Ogre::VertexElement::getTypeSize(Ogre::VET_FLOAT1),
                                                                        totalVerts,
                                                                        Ogre::HardwareBuffer::HBU_STATIC_WRITE_ONLY,
                                                                        false); //no shadow buff

}




//----------------------------------------------
#define SET_DELTA_AT(x, y, v)                       \
if ( mUseSkirts )   pDeltas[( y + 1)*vw+ x + 1] = v; \
else                pDeltas[( y)*vw+ x] = v;
void TerrainMesh::calculateDeltaValues() {

  using namespace Ogre;
  size_t vw = mWidth;
  if ( mUseSkirts ) vw += 2;

  //must be using morphing to use this function
  assert(g_heightMap->isMorphingEnabled());

  const size_t step = 2;

  // Create a set of delta values
  mDeltaBuffer = createDeltaBuffer();
  float* pDeltas = static_cast<float*>(mDeltaBuffer->lock(HardwareBuffer::HBL_DISCARD));
  memset(pDeltas, 0, (vw)*(vw) * sizeof(float));

  return;

  bool flag=false;
  for ( size_t y = 0; y < mWidth-step; y += step ) {
    for ( size_t x = 0; x < mWidth-step; x += step ) {
      //create the diffrence between the full vertex if the vertex wasn't their

      float bottom_left = getVertexHeight(x,y);
      float bottom_right = getVertexHeight(x+step,y);

      float top_left = getVertexHeight(x,y+step);
      float top_right = getVertexHeight(x+step,y+step);

      //const int vw = mWidth+2;
      SET_DELTA_AT(x, y+1, (bottom_left+top_left)/2 - getVertexHeight(x, y+1)) //left
        SET_DELTA_AT(x+2, y+1, (bottom_right+top_right)/2 - getVertexHeight(x+2, y+1)) //right

        SET_DELTA_AT(x+1, y+2, (top_right+top_left)/2 - getVertexHeight(x+1, y+2)) //top
        SET_DELTA_AT(x+1, y, (bottom_right+bottom_left)/2 - getVertexHeight(x+1, y)) //bottom

        //this might not be correct
        if ( !flag )
          SET_DELTA_AT(x+1, y+1, (bottom_left+top_right)/2 - getVertexHeight(x+1, y+1)) //center
            else
              SET_DELTA_AT(x+1, y+1, (bottom_right+top_left)/2 - getVertexHeight(x+1, y+1)) //center

                flag = !flag;
    }
    flag = !flag; //flip tries for next row
  }

  mDeltaBuffer->unlock();
  mVertexes->vertexBufferBinding->setBinding(DELTA_BINDING,mDeltaBuffer);

}
#undef SET_DELTA_AT

//----------------------------------------------
void TerrainMesh::calculateIndexValues() {
  using namespace Ogre;



  size_t vw = mWidth-1;
  if ( mUseSkirts ) vw += 2;

  const size_t indexCount = (vw)*(vw)*6;

  //need to manage allocation if not null
  assert(mIndices==0);

  mIndices = new IndexData();
  mIndices->indexCount = indexCount;
  mIndices->indexBuffer = HardwareBufferManager::getSingleton().createIndexBuffer(
                                                                                  HardwareIndexBuffer::IT_16BIT,
                                                                                  indexCount, HardwareBuffer::HBU_STATIC_WRITE_ONLY, false);

  unsigned short* indices = static_cast<unsigned short*>(mIndices->indexBuffer->lock(0,
                                                                                     mIndices->indexBuffer->getSizeInBytes(),
                                                                                     HardwareBuffer::HBL_DISCARD));

  bool flag = false;
  Ogre::uint indNum = 0;
  for ( Ogre::uint y = 0; y < (vw); y+=1 ) {
    for ( Ogre::uint x = 0; x < (vw); x+=1 ) {

      const int line1 = y * (vw+1) + x;
      const int line2 = (y + 1) * (vw+1) + x;

      if ( flag ) {
        *indices++ = line1;
        *indices++ = line2;
        *indices++ = line1 + 1;

        *indices++ = line1 + 1;
        *indices++ = line2;
        *indices++ = line2 + 1;
      } else {
        *indices++ = line1;
        *indices++ = line2;
        *indices++ = line2 + 1;

        *indices++ = line1;
        *indices++ = line2 + 1;
        *indices++ = line1 + 1;
      }
      flag = !flag; //flip tris for next time

      indNum+=6;
    }
    flag = !flag; //flip tries for next row
  }
  assert(indNum==indexCount);
  mIndices->indexBuffer->unlock();
  //return mIndices;
}
