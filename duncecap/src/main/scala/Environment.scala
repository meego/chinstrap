package DunceCap

import scala.collection.mutable

object Environment {
  val relationTypes = mutable.Map[String, List[String]]()
  var intersectionBuffers = List[String]()
  var prevIntersectionBuffersIndex = -1
  var intersectionBuffersIndex = -1

  def addRelationBinding(identifier : String, types : List[String]): Unit = {
    relationTypes += (identifier -> types)
  }

  def getTypes(identifier : String): List[String] = {
    relationTypes(identifier)
  }

  def resetIntersectionBuffer(choices : List[String]) = {
    assert(!choices.isEmpty)
    intersectionBuffers = choices
    intersectionBuffersIndex = 0
    prevIntersectionBuffersIndex = -1
  }

  def getIntersectionBuffer() = {
    intersectionBuffers(intersectionBuffersIndex)
  }

  def nextIntersectionBuffer() = {
    assert(intersectionBuffersIndex != -1)
    prevIntersectionBuffersIndex = intersectionBuffersIndex
     if (intersectionBuffersIndex + 1 >= intersectionBuffers.size) {
       intersectionBuffersIndex = 0
     } else {
       intersectionBuffersIndex += 1
     }
  }

  def getPrevIntersectionBuffer() = {
    assert(prevIntersectionBuffersIndex != -1)
    intersectionBuffers(prevIntersectionBuffersIndex)
  }
}
